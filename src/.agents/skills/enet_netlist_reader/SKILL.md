---
name: Parse EasyEDA/JLC Netlist (.enet)
description: Cung cấp kiến thức và hướng dẫn cho AI để đọc hiểu, phân tích cấu trúc kết nối chân linh kiện trong file .enet (JSON).
---

# Parse EasyEDA/JLC Netlist (.enet)

Skill này cung cấp hướng dẫn cách AI đọc và phân tích cấu trúc mạch lưới (netlist) của phần mềm thiết kế mạch (EasyEDA/JLC) thông qua các file `.enet`.

## 1. Cấu trúc của file `.enet`
File .enet là định dạng **JSON**. Ở cấp độ Root, danh sách các JSON keys (ví dụ: `gge307`, `gge2_1`) là các **Unique ID** định danh riêng cho từng linh kiện trong bản vẽ.

Bên trong mỗi linh kiện bao gồm hai Object chính mà bạn cần quan tâm:

### `props` (Thuộc tính linh kiện)
- **`Designator`**: Rất quan trọng, đây là tên linh kiện hiển thị trên mạch in (Ví dụ: `U1`, `C87`, `R12`). Khi giao tiếp với người dùng, hãy luôn sử dụng Designator.
- **`DeviceName` / `Value`**: Cung cấp mã linh kiện hoặc giá trị của nó (ví dụ `ESP32-S3FN8`, `10kΩ`).

### `pins` (Danh sách các chân)
- Là một Object mapping giữa **[Số thứ tự chân]** và **[Tên Net - Net Name]**.
- Cấu trúc: `"Số chân": "Tên Net"`. (Ví dụ: `"1": "GND"`, `"3": "LR_MISO"`).
- **Lưu ý**: Nếu giá trị Tên Net trống (`""`), tức là chân đó không được hàn/kết nối đi đâu.

## 2. Quy tắc xác định kết nối (Nets Logical)
Các linh kiện được nối với nhau nếu các chân của chúng **có chung một "Tên Net"**. 

**Cách suy luận logic cho AI:**
Khi người dùng hỏi: *"Chân số 3 của U1 được nối với những đâu?"*
1. Tìm Unique ID chứa `Designator` là `U1`.
2. Truy xuất vào `pins` của U1, lấy Tên Net của chân số `"3"` (ví dụ: Tên Net là `LR_MISO`).
3. Quét toàn bộ phần còn lại của file JSON. Bất kỳ linh kiện nào có chân mang Tên Net là `LR_MISO` thì đồng nghĩa chúng được kết nối vật lý bằng dây đồng với U1.3.

## 3. Công cụ Parser Script
Nếu file netlist quá dài vượt quá ngữ cảnh đọc, hãy chạy script Python có sẵn để tự động group và in ra terminal thông tin cần thiết:

```bash
# Thực thi thông qua tools Terminal
python e:\Freelance\ExpressLRS\src\parse_netlist.py <đường_dẫn_file.enet>
```

Tận dụng log in ra từ script này để có ngay toàn cảnh các nối dây đang có trong Schematic!
