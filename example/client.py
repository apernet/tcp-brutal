import socket
import time
import argparse

DEFAULT_PORT = 65432
DEFAULT_BUFFER_SIZE = 65536


def main(host, port, buf_size, rate_mbps):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        print(f"Connected to {host}:{port}")

        # Send the rate to the server
        s.sendall(rate_mbps.to_bytes(4, byteorder="big"))

        counter = 0
        start_time = time.time()

        try:
            while True:
                data = s.recv(buf_size)
                if not data:
                    break

                counter += len(data)

                current_time = time.time()
                if current_time - start_time >= 1:
                    speed_mbps = (
                        (counter * 8) / (1000 * 1000) / (current_time - start_time)
                    )
                    print(f"Current speed: {speed_mbps:.2f} Mbps")
                    counter = 0
                    start_time = current_time

        except KeyboardInterrupt:
            print("\nInterrupted by user")

        except Exception as e:
            print(f"Error: {e}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="TCP Brutal example client")
    parser.add_argument("host", help="Server host", type=str)
    parser.add_argument("rate_mbps", help="Rate in Mbps", type=int)
    parser.add_argument(
        "-p",
        "--port",
        help="Server port",
        type=int,
        default=DEFAULT_PORT,
    )
    parser.add_argument(
        "-b",
        "--buffer",
        help="Buffer size",
        type=int,
        default=DEFAULT_BUFFER_SIZE,
    )
    args = parser.parse_args()

    main(args.host, args.port, args.buffer, args.rate_mbps)
