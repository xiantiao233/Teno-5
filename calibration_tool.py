import serial
import struct
import threading
import time
import collections
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.widgets import TextBox, Button

# 根据你的 C 端物理通道映射顺序
SENSOR_ORDER = [
    "A7","C2","E7","D7","B6","A6","E6","D6","B5","A5","E5","D5","B4","A4","E4","D4",
    "B3","A3","C1","E3","D3","B2","A2","E2","D2","B1","E1","A1","D1","B8","A8","E8","D8","B7"
]

class CalibrationDebugger:
    def __init__(self, port_name, baud_rate, start_sensor, offset, on_save_callback):
        self.port_name = port_name
        self.baud_rate = baud_rate
        self.target = start_sensor
        self.offset = offset
        self.on_save_callback = on_save_callback  # 接收上位机传来的保存函数
        
        self.is_running = True
        self.data_raw = collections.deque(maxlen=300)
        self.data_lock = threading.Lock()
        self.current_raw_values = {name: 0 for name in SENSOR_ORDER}

    def _read_thread(self):
        try:
            self.ser = serial.Serial(self.port_name, self.baud_rate, timeout=1)
            self.ser.write(b'{DBON}') # 唤醒 STM32 透传模式
        except Exception as e:
            print(f">>> [调试器] 串口打开失败: {e}")
            return

        buffer = bytearray()
        while self.is_running:
            try:
                if self.ser.in_waiting > 0:
                    buffer.extend(self.ser.read(self.ser.in_waiting))
                    while len(buffer) >= 70:
                        if buffer[0] == 0x00:
                            checksum = sum(buffer[0:69]) & 0xFF
                            if checksum == buffer[69]:
                                channels = struct.unpack('<' + 'H'*34, buffer[1:69])
                                with self.data_lock:
                                    for i, name in enumerate(SENSOR_ORDER):
                                        self.current_raw_values[name] = channels[i]
                                    self.data_raw.append(self.current_raw_values[self.target])
                                buffer = buffer[70:]
                            else: buffer.pop(0)
                        else: buffer.pop(0)
                else:
                    time.sleep(0.001)
            except Exception:
                break

        # 退出时优雅关闭
        try:
            self.ser.write(b'{DBOF}')
            self.ser.close()
        except: pass

    def open_window(self):
        # 启动后台收发线程
        t = threading.Thread(target=self._read_thread, daemon=True)
        t.start()

        fig, ax = plt.subplots(figsize=(10, 6))
        plt.subplots_adjust(bottom=0.25)
        line_raw, = ax.plot([], [], label='Raw Capacitance', color='blue', linewidth=2)
        ax.legend(loc='upper right')

        def init_anim():
            ax.set_xlim(0, 300)
            ax.set_ylim(40000, 65000)
            return line_raw,

        def animate(i):
            with self.data_lock:
                y_data = list(self.data_raw)

            if not y_data: return line_raw,
            line_raw.set_data(range(len(y_data)), y_data)
            max_val, min_val = max(y_data), min(y_data)
            if max_val - min_val > 1000:
                ax.set_ylim(min_val - 500, max_val + 1000)
            else:
                avg = sum(y_data) / len(y_data)
                ax.set_ylim(avg - 2000, avg + 2000)
            ax.set_title(f"Monitor: [{self.target}] | Current Raw: {y_data[-1]}")
            return line_raw,

        def submit_sensor(text):
            text = text.upper()
            if text in SENSOR_ORDER:
                with self.data_lock:
                    self.target = text
                    self.data_raw.clear()

        ax_box = plt.axes([0.15, 0.08, 0.2, 0.075])
        txt_box = TextBox(ax_box, 'Sensor: ', initial=self.target)
        txt_box.on_submit(submit_sensor)

        def on_calibrate_click(event):
            with self.data_lock:
                y_data = list(self.data_raw)
            if len(y_data) < 50: return

            mean_val = sum(y_data) / len(y_data)
            new_thresh = int(mean_val + self.offset)
            
            # ===== 智能核心：调用上位机提供的保存逻辑 =====
            if self.on_save_callback:
                self.on_save_callback(self.target, new_thresh)

        ax_btn = plt.axes([0.5, 0.08, 0.25, 0.075])
        btn_cal = Button(ax_btn, '校准并智能存入 YAML')
        btn_cal.on_clicked(on_calibrate_click)

        # 阻塞在此，直到用户关闭波形窗口
        ani = animation.FuncAnimation(fig, animate, init_func=init_anim, interval=30, blit=False)
        plt.show()

        # 窗口关闭后清理标志位
        self.is_running = False
        t.join(timeout=1.0)