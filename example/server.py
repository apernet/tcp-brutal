import socket
import struct
import threading
import time
import argparse

TCP_CONGESTION = 13
TCP_BRUTAL_PARAMS = 23301

DEFAULT_PORT = 65432
DEFAULT_BUFFER_SIZE = 65536


def client_thread(conn, addr, duration, buffer_size, rate):
    print(f"Connected by {addr}")
    start_time = time.time()

    cwnd_gain = 15
    brutal_params_value = struct.pack("QI", rate, cwnd_gain)
    conn.setsockopt(socket.IPPROTO_TCP, TCP_BRUTAL_PARAMS, brutal_params_value)

    try:
        while time.time() - start_time < duration:
            data = bytearray(buffer_size)
            conn.sendall(data)
    except Exception as e:
        print(f"Error sending data: {e}")
    finally:
        conn.close()
        print(f"Disconnected {addr}")


def main():
    parser = argparse.ArgumentParser(
        description="TCP Brutal example server",
    )
    parser.add_argument(
        "-l", "--listen", type=str, default="", help="Address to listen on"
    )
    parser.add_argument(
        "-p", "--port", type=int, default=DEFAULT_PORT, help="Port to listen on"
    )
    parser.add_argument(
        "-d", "--duration", type=int, default=10, help="Send duration in seconds"
    )
    parser.add_argument(
        "-b",
        "--buffer-size",
        type=int,
        default=DEFAULT_BUFFER_SIZE,
        help="Buffer size",
    )

    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.IPPROTO_TCP, TCP_CONGESTION, "brutal".encode())
        s.bind((args.listen, args.port))
        s.listen()

        print(f"Server listening on {args.listen}:{args.port}")

        try:
            while True:
                conn, addr = s.accept()

                rate_bytes = conn.recv(4)
                if not rate_bytes:
                    conn.close()
                    continue

                rate = struct.unpack("!I", rate_bytes)[0]
                rate = int(rate * 1000 * 1000 / 8)  # Convert Mbps to bytes per second

                thread = threading.Thread(
                    target=client_thread,
                    args=(conn, addr, args.duration, args.buffer_size, rate),
                )
                thread.start()
        except KeyboardInterrupt:
            print("\nServer is shutting down.")


if __name__ == "__main__":
    main()
