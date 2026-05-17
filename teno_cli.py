import struct
import yaml
import os
import serial
import serial.tools.list_ports
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext
import subprocess
import platform

# 引入波形调试器，同时引入 SENSOR_ORDER 用来做物理通道的索引映射
from calibration_tool import CalibrationDebugger, SENSOR_ORDER

# ================= 1. 严格一字节对齐的数据流结构解析格式 =================
# < 表示无填充小端字节序
# 基础控制: I(uint32), H(uint16), B(uint8)
# 各区阈值与突变参数: 17个独立命名的 i(int32_t)
# 浮点检测系数: 2个 f(float)
# 物理通道: 34个 1sQiii
CHANNEL_FMT = "1sQiii"
CONFIG_FMT = "<IHB17i2f" + CHANNEL_FMT * 34
PAYLOAD_SIZE = struct.calcsize(CONFIG_FMT) # 797 字节

YAML_FILENAME = "teno_config.yaml"

def calc_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def unpack_config(payload: bytes) -> dict:
    """将下位机送上来的裸二进制包解构映射为可读字典"""
    u = struct.unpack(CONFIG_FMT, payload)
    config = {
        "magic": u[0], "version": u[1], "enable_fixed_trigger_mode": u[2],
        "fixed_trigger_default_a": u[3], "fixed_trigger_default_b": u[4],
        "fixed_trigger_default_c": u[5], "fixed_trigger_default_d": u[6],
        "fixed_trigger_default_e": u[7],
        "variance_thresh_bcde": u[8], "variance_thresh_bcde_down": u[9],
        "var_thresh_b": u[10], "var_thresh_c": u[11], "var_thresh_d": u[12], "var_thresh_e": u[13],
        "variance_thresh_a_default": u[14], "var001_default": u[15],
        "area_a_release_drop_thresh": u[16], "area_a_press_rise_thresh": u[17],
        "area_a_press_break_thresh": u[18], "area_a_fast_slide_fps_limit": u[19],
        "rea_a_down_tr_up": round(u[20], 3), "rea_a_down_tr_down": round(u[21], 3),
        "channels": []
    }
    idx = 22
    for _ in range(34):
        config["channels"].append({
            "block": u[idx].decode('ascii'), "mask": u[idx+1], "default_thresh": u[idx+2],
            "var_thresh": u[idx+3], "var001_thresh": u[idx+4]
        })
        idx += 5
    return config

def pack_config(config: dict) -> bytes:
    """将明文解耦后的 YAML 属性序列化为紧凑一字节对齐二进制串"""
    ch_list = []
    for ch in config["channels"]:
        ch_list.extend([ch["block"].encode('ascii'), ch["mask"], ch["default_thresh"], ch["var_thresh"], ch["var001_thresh"]])
    return struct.pack(
        CONFIG_FMT,
        config["magic"], config["version"], config["enable_fixed_trigger_mode"],
        config["fixed_trigger_default_a"], config["fixed_trigger_default_b"], config["fixed_trigger_default_c"], config["fixed_trigger_default_d"], config["fixed_trigger_default_e"],
        config["variance_thresh_bcde"], config["variance_thresh_bcde_down"], config["var_thresh_b"], config["var_thresh_c"], config["var_thresh_d"], config["var_thresh_e"],
        config["variance_thresh_a_default"], config["var001_default"],
        config["area_a_release_drop_thresh"], config["area_a_press_rise_thresh"], config["area_a_press_break_thresh"], config["area_a_fast_slide_fps_limit"],
        config["rea_a_down_tr_up"], config["rea_a_down_tr_down"],
        *ch_list
    )

class TenoApp:
    def __init__(self, root):
        self.root = root
        self.root.title("TenoDX 参数可视化调参系统 V2.0")
        self.root.geometry("760x480")  # 稍微加宽了一点以容纳新按钮
        self.root.configure(padx=15, pady=15)
        
        top_frame = ttk.Frame(root)
        top_frame.pack(fill=tk.X, pady=(0, 15))
        ttk.Label(top_frame, text="主控板端口:").pack(side=tk.LEFT)
        self.port_combo = ttk.Combobox(top_frame, width=32, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=10)
        ttk.Button(top_frame, text="🔄 刷新物理串口", command=self.refresh_ports).pack(side=tk.LEFT)
        
        mid_frame = ttk.LabelFrame(root, text=" 核心配置映射同步 ", padding=10)
        mid_frame.pack(fill=tk.X, pady=(0, 15))
        
        # 你的原有按钮
        ttk.Button(mid_frame, text="📥 备份读取现有固件", command=lambda: self.run_thread(self.task_load)).pack(side=tk.LEFT, padx=5)
        ttk.Button(mid_frame, text="📤 全量一键同步下发", command=lambda: self.run_thread(self.task_save)).pack(side=tk.LEFT, padx=5)
        
        # 新增的波形校准按钮！
        ttk.Button(mid_frame, text="📊 启动波形校准", command=self.task_open_debugger).pack(side=tk.LEFT, padx=5)
        
        ttk.Button(mid_frame, text="📝 编辑物理配置", command=self.open_yaml).pack(side=tk.RIGHT, padx=5)
        
        self.log_area = scrolledtext.ScrolledText(root, height=18, state='disabled', bg="#1C1C1C", fg="#00FF66", font=("Consolas", 10))
        self.log_area.pack(fill=tk.BOTH, expand=True)
        
        self.refresh_ports()
        self.log("[控制台信息] 离线配置文件模式已载入。")

    def log(self, msg):
        self.log_area.config(state='normal')
        self.log_area.insert(tk.END, msg + "\n")
        self.log_area.see(tk.END)
        self.log_area.config(state='disabled')

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [f"{p.device} - {p.description}" for p in ports]
        self.port_combo['values'] = port_list
        if port_list: self.port_combo.current(0)
        else: self.port_combo.set("未检测到有效设备，请插紧外设排线")

    def get_selected_port(self):
        val = self.port_combo.get()
        if not val or "未检测" in val: return None
        return val.split(" - ")[0]

    def open_yaml(self):
        if not os.path.exists(YAML_FILENAME):
            self.log(f"❌ 找不到本地模板: {YAML_FILENAME}")
            return
        if platform.system() == 'Windows': os.startfile(YAML_FILENAME)
        else: subprocess.call(('open' if platform.system() == 'Darwin' else 'xdg-open', YAML_FILENAME))

    def run_thread(self, target):
        threading.Thread(target=target, daemon=True).start()

    def task_load(self):
        port = self.get_selected_port()
        if not port: return
        self.log(f"正在建立 CDC 握手通道，请求从 {port} 拷贝物理 Flash 扇区数据...")
        try:
            with serial.Serial(port, 115200, timeout=2.0) as ser:
                req = bytes([0x5A, 0xA5, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xED, 0xDE])
                ser.write(req)
                res = ser.read(5 + PAYLOAD_SIZE + 4)
                if len(res) < 9:
                    self.log("❌ 握手超时，STM32 未处于待命配置状态。")
                    return
                
                payload = res[5 : 5+PAYLOAD_SIZE]
                recv_crc = res[5+PAYLOAD_SIZE] | (res[6+PAYLOAD_SIZE] << 8)
                if calc_crc16(payload) != recv_crc:
                    self.log("❌ 接收流数据校验和错位。")
                    return
                    
                config_dict = unpack_config(payload)
                with open(YAML_FILENAME, 'w', encoding='utf-8') as f:
                    yaml.dump(config_dict, f, default_flow_style=False, sort_keys=False, allow_unicode=True)
                self.log(f"✅ 下位机参数同步接收成功！已输出至 '{YAML_FILENAME}'。")
        except Exception as e: self.log(f"❌ 通讯发生不可达异常: {e}")

    def task_save(self):
        port = self.get_selected_port()
        if not port: return
        try:
            with open(YAML_FILENAME, 'r', encoding='utf-8') as f:
                config_dict = yaml.safe_load(f)
            payload = pack_config(config_dict)
            length = len(payload)
            crc = calc_crc16(payload)
            
            frame = bytearray([0x5A, 0xA5, 0x02, length & 0xFF, (length >> 8) & 0xFF])
            frame.extend(payload)
            frame.extend([crc & 0xFF, (crc >> 8) & 0xFF])
            frame.extend([0xED, 0xDE])
            
            with serial.Serial(port, 115200, timeout=2.0) as ser:
                ser.write(frame)
                res = ser.read(9)
                if len(res) == 9 and res[0] == 0x5A and res[1] == 0xA5 and res[2] == 0x03:
                    self.log("✅ 下位机主控核对无误：CRC16 码一致，配置已写入芯片内部 Page63 并完成热生效机制！")
                else: self.log("❌ 写入遭到内部状态机断开或超时。")
        except Exception as e: self.log(f"❌ 编译或封包解析故障: {e}")

    # ================= 新增：智能波形校准启动逻辑 =================
    def task_open_debugger(self):
        port = self.get_selected_port()
        if not port:
            self.log("❌ 启动失败：请先在下拉框中选择正确的串口！")
            return

        def handle_calibration_save(sensor_name, new_threshold):
            """当波形图点击'校准'后，会触发这个函数"""
            try:
                if not os.path.exists(YAML_FILENAME):
                    self.log(f"❌ 找不到 {YAML_FILENAME}，请先点击【备份读取现有固件】生成配置！")
                    return
                
                # 1. 读出当前的 YAML
                with open(YAML_FILENAME, 'r', encoding='utf-8') as f:
                    config_dict = yaml.safe_load(f)
                
                # 2. 精准定位目标通道索引
                if sensor_name in SENSOR_ORDER:
                    idx = SENSOR_ORDER.index(sensor_name)
                    # 3. 智能修改 default_thresh (灵敏度阈值)
                    old_val = config_dict["channels"][idx]["default_thresh"]
                    config_dict["channels"][idx]["default_thresh"] = new_threshold
                    
                    # 4. 原样保存回去
                    with open(YAML_FILENAME, 'w', encoding='utf-8') as f:
                        yaml.dump(config_dict, f, default_flow_style=False, sort_keys=False, allow_unicode=True)
                    
                    self.log(f"✅ 【校准成功】 {sensor_name} 阈值: {old_val} -> {new_threshold} (已写入 YAML)")
                    self.log("💡 提示：关掉波形图后，点击主界面的【全量一键同步下发】即可烧入单片机。")
                else:
                    self.log(f"❌ 未知的传感器通道: {sensor_name}")

            except Exception as e:
                self.log(f"❌ 写入 YAML 发生异常: {e}")

        # 启动调试器
        self.log(f"🚀 正在 {port} 上启动波形调试器（主界面将安全锁定直至波形图关闭）...")
        
        # 实例化调试器（传入从 UI 获取的物理串口）
        debugger = CalibrationDebugger(
            port_name=port, 
            baud_rate=115200, 
            start_sensor='A3', 
            offset=1500, 
            on_save_callback=handle_calibration_save
        )
        
        # 阻塞打开窗口，避免多线程抢占串口
        debugger.open_window() 
        self.log("🛑 波形调试器已关闭，串口控制权已安全交还。")


if __name__ == "__main__":
    root = tk.Tk()
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
    except: pass
    app = TenoApp(root)
    root.mainloop()