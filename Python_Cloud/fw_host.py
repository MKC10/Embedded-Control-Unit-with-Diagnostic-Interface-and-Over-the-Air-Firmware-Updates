import sys
import json
import struct
import time
import zlib
import hashlib
import tempfile
import os
import serial
import boto3
from botocore.exceptions import ClientError, NoCredentialsError
S3_BUCKET = 'firmwareupdatedualbankbootloader-064123638401-us-east-2-an'
S3_REGION = 'us-east-2'
S3_PREFIX = 'Firmware_STM32_Updtae'
FIRMWARE_KEY = f'{S3_PREFIX}/ECU_Firmware.bin'
_s3_client = None

def get_s3_client():
    global _s3_client
    if _s3_client is None:
        _s3_client = boto3.client('s3', region_name=S3_REGION)
    return _s3_client

def fetch_s3_json(key: str) -> dict:
    s3 = get_s3_client()
    try:
        resp = s3.get_object(Bucket=S3_BUCKET, Key=key)
        return json.loads(resp['Body'].read().decode('utf-8'))
    except NoCredentialsError:
        raise RuntimeError('No AWS credentials found. Run `aws configure` or set AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY environment variables.')
    except ClientError as e:
        code = e.response.get('Error', {}).get('Code', 'Unknown')
        if code in ('403', 'AccessDenied'):
            raise RuntimeError(f'Access denied fetching s3://{S3_BUCKET}/{key} — check the IAM user/role has s3:GetObject on this bucket.')
        elif code in ('404', 'NoSuchKey'):
            raise RuntimeError(f's3://{S3_BUCKET}/{key} does not exist.')
        raise
CMD_START = 1
CMD_DATA = 2
CMD_END = 3
CMD_STATUS = 4
ACK = 6
NAK = 21
CHUNK_SIZE = 256
META_STRUCT_FORMAT = '<6IBB2sI'
META_STRUCT_SIZE = struct.calcsize(META_STRUCT_FORMAT)
BANK_PENDING = 2
BANK_STATUS_NAMES = {255: 'BANK_EMPTY', 2: 'BANK_PENDING', 3: 'BANK_TRIAL', 4: 'BANK_CONFIRMED', 5: 'BANK_FAILED'}
TIMEOUT_START = 10.0
TIMEOUT_DATA = 2.0
TIMEOUT_END = 5.0
TIMEOUT_STATUS = 5.0
RETRIES = 3
timing_log = {'START': [], 'DATA': [], 'END': []}

def open_port(port: str, baud: int=115200) -> serial.Serial:
    ser = serial.Serial(port, baud, timeout=0.5)
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser

def wait_ack(ser: serial.Serial, timeout: float, label: str, category: str) -> bool:
    deadline = time.time() + timeout
    start_t = time.perf_counter()
    line_buf = bytearray()

    def flush_line():
        if line_buf:
            text = line_buf.decode('ascii', errors='replace').rstrip('\r\n')
            if text:
                print(f'  [STM32] {text}')
            line_buf.clear()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        byte_val = b[0]
        if byte_val == ACK:
            flush_line()
            elapsed = time.perf_counter() - start_t
            print(f'{label}: ACK   (elapsed: {elapsed:.3f}s)')
            timing_log[category].append(elapsed)
            return True
        elif byte_val == NAK:
            flush_line()
            elapsed = time.perf_counter() - start_t
            print(f'{label}: NAK   (elapsed: {elapsed:.3f}s)')
            timing_log[category].append(elapsed)
            return False
        elif byte_val in (10, 13):
            flush_line()
        else:
            line_buf.append(byte_val)
    flush_line()
    print(f'{label}: TIMEOUT (no response within {timeout}s)')
    return False

def send_start(ser: serial.Serial, total_size: int) -> bool:
    for attempt in range(1, RETRIES + 1):
        print(f'Sending START (size={total_size}), attempt {attempt}/{RETRIES}...')
        ser.reset_input_buffer()
        packet = bytes([CMD_START]) + struct.pack('<IH', total_size, 0)
        ser.write(packet)
        ser.flush()
        if wait_ack(ser, TIMEOUT_START, 'START', 'START'):
            return True
        print('START not acknowledged — retrying...')
    return False

def send_data_chunk(ser: serial.Serial, chunk: bytes, seq: int, total_chunks: int) -> bool:
    for attempt in range(1, RETRIES + 1):
        print(f'Sending DATA chunk {seq}/{total_chunks} ({len(chunk)} bytes), attempt {attempt}/{RETRIES}...')
        ser.reset_input_buffer()
        packet = bytes([CMD_DATA]) + struct.pack('<H', len(chunk)) + chunk
        ser.write(packet)
        ser.flush()
        if wait_ack(ser, TIMEOUT_DATA, f'DATA[{seq}]', 'DATA'):
            return True
        print(f'DATA chunk {seq} not acknowledged — retrying...')
    return False

def send_end(ser: serial.Serial, expected_crc: int) -> bool:
    for attempt in range(1, RETRIES + 1):
        print(f'Sending END (crc=0x{expected_crc:08X}), attempt {attempt}/{RETRIES}...')
        ser.reset_input_buffer()
        packet = bytes([CMD_END]) + struct.pack('<I', expected_crc)
        ser.write(packet)
        ser.flush()
        if wait_ack(ser, TIMEOUT_END, 'END', 'END'):
            return True
        print('END not acknowledged — retrying...')
    return False

def query_and_verify_status(ser: serial.Serial, expected_crc: int, expected_size: int) -> bool:
    print()
    print('Verifying via STATUS read-back...')
    ser.reset_input_buffer()
    ser.write(bytes([CMD_STATUS]))
    ser.flush()
    deadline = time.time() + TIMEOUT_STATUS
    data = b''
    while len(data) < META_STRUCT_SIZE and time.time() < deadline:
        chunk = ser.read(META_STRUCT_SIZE - len(data))
        if chunk:
            data += chunk
    if len(data) != META_STRUCT_SIZE:
        print(f'STATUS: INCOMPLETE ({len(data)}/{META_STRUCT_SIZE} bytes)')
        return False
    magic, bankA_size, bankA_crc32, bankB_version, bankB_size, bankB_crc32, bankB_status, bankB_trial_count, _reserved, self_crc = struct.unpack(META_STRUCT_FORMAT, data)
    status_name = BANK_STATUS_NAMES.get(bankB_status, f'UNKNOWN(0x{bankB_status:02X})')
    print(f'  bankB_status    = {status_name}')
    print(f'  bankB_size      = {bankB_size} (expected {expected_size})')
    print(f'  bankB_crc32     = 0x{bankB_crc32:08X} (expected 0x{expected_crc:08X})')
    print(f'  bankB_trial_cnt = {bankB_trial_count}')
    return bankB_status == BANK_PENDING and bankB_size == expected_size and (bankB_crc32 == expected_crc)

def print_timing_summary():
    print()
    print('=' * 50)
    print('TIMING SUMMARY')
    print('=' * 50)
    for category, samples in timing_log.items():
        if not samples:
            print(f'{category}: no samples')
            continue
        print(f'{category}: n={len(samples)}  min={min(samples):.3f}s  max={max(samples):.3f}s  avg={sum(samples) / len(samples):.3f}s')
    print('=' * 50)

def run_ota_update(port: str, firmware_path: str) -> bool:
    with open(firmware_path, 'rb') as f:
        firmware = f.read()
    total_size = len(firmware)
    expected_crc = zlib.crc32(firmware) & 4294967295
    print(f'Firmware: {firmware_path}')
    print(f'Size: {total_size} bytes')
    print(f'CRC-32: 0x{expected_crc:08X}')
    print()
    ser = open_port(port)
    overall_start = time.perf_counter()
    try:
        if not send_start(ser, total_size):
            print('\nFLASH FAILURE — STM32 did not accept START.')
            print('Check: board entered OTA mode? (OTA RX READY seen with')
            print('the blue button held through reset?) Correct COM port free?')
            return False
        total_chunks = (total_size + CHUNK_SIZE - 1) // CHUNK_SIZE
        for seq in range(total_chunks):
            offset = seq * CHUNK_SIZE
            chunk = firmware[offset:offset + CHUNK_SIZE]
            if not send_data_chunk(ser, chunk, seq + 1, total_chunks):
                print(f'\nFLASH FAILURE — chunk {seq + 1}/{total_chunks} failed after {RETRIES} attempts.')
                return False
        if not send_end(ser, expected_crc):
            print('\nFLASH FAILURE — STM32 rejected the final image.')
            return False
        verified = query_and_verify_status(ser, expected_crc, total_size)
        overall_elapsed = time.perf_counter() - overall_start
        print(f'\nTotal transfer time: {overall_elapsed:.2f}s ({total_size} bytes, {total_size / overall_elapsed:.0f} B/s effective)')
        print_timing_summary()
        return verified
    finally:
        ser.close()

class OtaState:
    IDLE, CHECK, DOWNLOAD, VERIFY, FLASH, COMMIT, DONE, FAILED = ('IDLE', 'CHECK', 'DOWNLOAD', 'VERIFY', 'FLASH', 'COMMIT', 'DONE', 'FAILED')

def log_state(state: str, msg: str=''):
    print(f'[{state}] {msg}' if msg else f'[{state}]')

def check_for_update(current_version: str):
    log_state(OtaState.CHECK, f'Fetching s3://{S3_BUCKET}/{S3_PREFIX}/latest.json')
    latest = fetch_s3_json(f'{S3_PREFIX}/latest.json')
    latest_version = latest.get('latest_version')
    if not latest_version:
        raise ValueError("latest.json has no 'latest_version' field")
    log_state(OtaState.CHECK, f'Latest published: {latest_version} | device reports: {current_version}')
    if latest_version == current_version:
        return (None, latest_version)
    meta_key = f'{S3_PREFIX}/metadata.json'
    log_state(OtaState.CHECK, f'Fetching s3://{S3_BUCKET}/{meta_key}')
    metadata = fetch_s3_json(meta_key)
    for field in ('version', 'firmware_size', 'sha256'):
        if field not in metadata:
            raise ValueError(f"metadata.json missing required field '{field}'")
    return (metadata, latest_version)

def download_firmware(metadata: dict, version: str) -> str:
    s3 = get_s3_client()
    key = FIRMWARE_KEY
    log_state(OtaState.DOWNLOAD, f'Downloading s3://{S3_BUCKET}/{key}')
    try:
        resp = s3.get_object(Bucket=S3_BUCKET, Key=key)
        data = resp['Body'].read()
    except NoCredentialsError:
        raise RuntimeError('No AWS credentials found. Run `aws configure` or set AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY environment variables.')
    except ClientError as e:
        code = e.response.get('Error', {}).get('Code', 'Unknown')
        raise RuntimeError(f'Failed to download s3://{S3_BUCKET}/{key}: {code}')
    fd, path = tempfile.mkstemp(suffix='.bin', prefix=f'fw_{version}_')
    with os.fdopen(fd, 'wb') as f:
        f.write(data)
    log_state(OtaState.DOWNLOAD, f'{len(data)} bytes -> {path}')
    return path

def verify_firmware(path: str, metadata: dict) -> bool:
    with open(path, 'rb') as f:
        data = f.read()
    expected_size = int(metadata['firmware_size'])
    if len(data) != expected_size:
        log_state(OtaState.VERIFY, f'SIZE MISMATCH: got {len(data)}, expected {expected_size}')
        return False
    sha = hashlib.sha256(data).hexdigest()
    expected_sha = metadata['sha256'].lower()
    log_state(OtaState.VERIFY, f'SHA256 computed: {sha}')
    log_state(OtaState.VERIFY, f'SHA256 expected: {expected_sha}')
    if sha != expected_sha:
        log_state(OtaState.VERIFY, 'SHA256 MISMATCH — refusing to flash')
        return False
    log_state(OtaState.VERIFY, 'OK — size and SHA256 match')
    return True

def run_cloud_ota(port: str, current_version: str) -> bool:
    log_state(OtaState.IDLE, 'Cloud OTA agent starting')
    try:
        metadata, latest_version = check_for_update(current_version)
    except RuntimeError as e:
        log_state(OtaState.FAILED, str(e))
        return False
    except Exception as e:
        log_state(OtaState.FAILED, f'CHECK failed: {e}')
        return False
    if metadata is None:
        log_state(OtaState.DONE, f'Already up to date ({latest_version}) — nothing to do')
        return True
    try:
        fw_path = download_firmware(metadata, latest_version)
    except RuntimeError as e:
        log_state(OtaState.FAILED, str(e))
        return False
    except Exception as e:
        log_state(OtaState.FAILED, f'DOWNLOAD failed: {e}')
        return False
    try:
        if not verify_firmware(fw_path, metadata):
            log_state(OtaState.FAILED, 'VERIFY failed — aborting before flash')
            return False
        log_state(OtaState.FLASH, f"Delivering {metadata['version']} to STM32 on {port} via UART")
        ok = run_ota_update(port, fw_path)
        if not ok:
            log_state(OtaState.FAILED, 'FLASH stage failed — see transfer log above')
            return False
        log_state(OtaState.COMMIT, "Image staged as BANK_PENDING. Reset the board to trial-boot; the app's FW_Update_ConfirmBoot() completes the commit.")
        log_state(OtaState.DONE, f"Cloud OTA pipeline complete for {metadata['version']}")
        return True
    finally:
        try:
            os.unlink(fw_path)
        except OSError:
            pass
if __name__ == '__main__':
    if len(sys.argv) >= 2 and (not sys.argv[1].startswith('--')):
        port_arg = sys.argv[1]
    else:
        port_arg = 'COM5'
        print(f'No COM port given — defaulting to {port_arg}')
    current = 'v0.0.0'
    if '--current-version' in sys.argv:
        idx = sys.argv.index('--current-version')
        if idx + 1 < len(sys.argv):
            current = sys.argv[idx + 1]
    success = run_cloud_ota(port_arg, current)
    sys.exit(0 if success else 1)