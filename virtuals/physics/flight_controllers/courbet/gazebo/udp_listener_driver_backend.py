import socket, threading, time
import numpy as np

class DriverMagUDPReceiver:
    def __init__(self, bind_ip: str = "127.0.0.1", port: int = 15150):
        self.bind_ip = bind_ip
        self.port = port
        self._sock = None
        self._th = None
        self._stop = False
        self._lock = threading.Lock()
        self._latest_mg = np.zeros(3, dtype=float)
        self._latest_time = 0.0

    def start(self):
        if self._th is not None:
            return
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind((self.bind_ip, self.port))
        self._sock.settimeout(0.5)
        self._th = threading.Thread(target=self._loop, daemon=True)
        self._th.start()

    def stop(self):
        self._stop = True
        if self._th is not None:
            self._th.join(timeout=1.0)
        if self._sock is not None:
            self._sock.close()

    def _loop(self):
        while not self._stop:
            try:
                data, _addr = self._sock.recvfrom(1024)
            except socket.timeout:
                continue
            except OSError:
                break

            try:
                s = data.decode("utf-8").strip().replace(",", " ")
                parts = s.split()
                if len(parts) != 3:
                    continue
                xyz = np.array([float(parts[0]), float(parts[1]), float(parts[2])], dtype=float)
            except Exception:
                continue

            with self._lock:
                self._latest_mg = xyz
                self._latest_time = time.time()

    def latest_milli_gauss(self) -> tuple[np.ndarray, float]:
        with self._lock:
            return self._latest_mg.copy(), float(self._latest_time)
    
if __name__ == "__main__":
    receiver = DriverMagUDPReceiver()
    receiver.start()
    print("DriverMagUDPReceiver started. Listening for magnetometer data...")
    try:
        while True:
            time.sleep(2.0)
            mg, t = receiver.latest_milli_gauss()
            print(f"Latest Magnetometer (mG): X={mg[0]:.3f}, Y={mg[1]:.3f}, Z={mg[2]:.3f} at time {t:.3f}")
    except KeyboardInterrupt:
        print("Stopping Receiver...")
    finally:
        receiver.stop()
        print("Receiver stopped.")