import socket
import struct
import numpy as np
from collections import deque
import time

UDP_IP = "0.0.0.0"
UDP_PORT = 5555

WINDOW_SIZE = 50
THRESHOLD = 10
COOLDOWN_SECONDS = 1.0

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))

    dq = deque(maxlen=WINDOW_SIZE)
    last_detect_time = 0

    print(f"Listening for CSI on Port {UDP_PORT}...")

    while True:
        data, addr = sock.recvfrom(1024)

        if len(data) < 3:
            continue

        header_bytes = data[:3]
        rssi, length = struct.unpack('<bH', header_bytes)

        payload_bytes = data[3 : 3+length]

        if len(payload_bytes) != length:
            continue

        csi_format = f"<{length}b"
        raw_iq_data = struct.unpack(csi_format, payload_bytes)

        arr = np.array(raw_iq_data)
        I = arr[0::2]
        Q = arr[1::2]
        A = np.abs(I + 1j * Q)

        dq.append(A)
        if (len(dq) == WINDOW_SIZE):
            history_matrix = np.array(dq)
            variance_array = np.var(history_matrix[5:WINDOW_SIZE-5], axis=0)
            var = np.sum(variance_array)
            if var > THRESHOLD:
                current_time = time.time()
                if current_time - last_detect_time > COOLDOWN_SECONDS:
                    print(f"MOVEMENT DETECTED! (Variance: {var:.2f})")
                    last_detect_time = current_time
        pass


if __name__ == "__main__":
    main()
