import json
import sys
from collections import defaultdict

def parse_netlist(file_path):
    # Đọc file netlist (có định dạng JSON)
    with open(file_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    devices = {}
    nets = defaultdict(list)

    # Duyệt qua từng linh kiện (cấp đầu tiên là unique id)
    for unique_id, device_data in data.items():
        props = device_data.get('props', {})
        pins = device_data.get('pins', {})
        
        # Lấy Designator (ví dụ U1, C87) và Tên linh kiện
        designator = props.get('Designator', 'Unknown')
        device_name = props.get('DeviceName', props.get('LCSC Part Name', 'Unknown'))
        
        devices[unique_id] = {
            'designator': designator,
            'name': device_name,
            'pins': pins
        }
        
        # Duyệt qua các chân (pins) để tìm các kết nối (nets)
        for pin_num, pin_name in pins.items():
            if pin_name: # Bỏ qua các chân không có tên (không được kết nối)
                # Lưu thông tin chân vào net tương ứng
                nets[pin_name].append(f"{designator}.{pin_num}")

    return devices, nets

def main():
    if len(sys.argv) < 2:
        print("Sử dụng: python parse_netlist.py <đường_dẫn_đến_file_enet>")
        sys.exit(1)
        
    file_path = sys.argv[1]
    print(f"Đang đọc file: {file_path}...\n")
    try:
        devices, nets = parse_netlist(file_path)
    except FileNotFoundError:
        print(f"Không tìm thấy file: {file_path}")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Lỗi: File {file_path} không phải là JSON hợp lệ.")
        sys.exit(1)
    
    # In ra danh sách các thiết bị
    print("=== DANH SÁCH LINH KIỆN ===")
    for uid, info in devices.items():
        print(f"[{uid}] {info['designator']:<5} -> {info['name']}")
        
    # In ra các kết nối (những chân có cùng tên)
    print("\n=== DANH SÁCH CÁC KẾT NỐI (NETS) ===")
    for net_name, pins in nets.items():
        if len(pins) > 1:
            print(f"Net '{net_name}': liên kết các chân -> {', '.join(pins)}")
        elif len(pins) == 1:
            print(f"Net '{net_name}' (Chưa kết nối): -> {pins[0]}")

if __name__ == '__main__':
    main()
