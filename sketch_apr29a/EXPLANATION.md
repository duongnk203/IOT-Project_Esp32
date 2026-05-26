# Giải thích Logic Code - ESP32 IoT Car

Tài liệu này giải thích chi tiết cấu trúc và logic hoạt động của file `sketch_apr29a.ino` dùng cho ESP32 trong hệ thống xe IoT.

---

## 1. Kiến trúc tổng thể
Hệ thống sử dụng **FreeRTOS** để chia nhỏ các chức năng thành 5 Task chạy song song trên 2 nhân của ESP32:
- **TaskWeb**: Quản lý giao diện điều khiển tại chỗ (Local Web Server).
- **TaskPM25**: Đọc dữ liệu bụi mịn từ cảm biến qua UART.
- **TaskEnv**: Đọc cảm biến DHT11, cảm biến khói, xử lý logic Relay và gửi dữ liệu lên Cloud API.
- **TaskAPI**: Đồng bộ cấu hình và nhận lệnh điều khiển từ Backend Cloud.
- **TaskControl**: Lõi điều phối chuyển động của xe (Auto/Manual).

---

## 2. Các chế độ điều khiển

### A. Chế độ Tự động (AUTO_MODE)
Xe hoạt động dựa trên logic:
1.  **Tránh vật cản**: Nếu cảm biến siêu âm chính giữa (`distC`) phát hiện vật cản quá gần, xe sẽ lùi lại và xoay sang bên thoáng hơn.
2.  **PID bám hướng**: Tính toán sai lệch giữa cảm biến trái (`distL`) và phải (`distR`) để điều chỉnh tốc độ bánh trái/phải, giúp xe đi thẳng giữa hành lang hoặc bám tường đều.
3.  **Điều tốc thông minh**: Tự động giảm tốc độ khi tiến gần vật cản phía trước để phản ứng mượt mà hơn.

### B. Chế độ Thủ công (MANUAL_MODE)
Khi chuyển sang chế độ này, logic tự động bị ngắt hoàn toàn:
- Xe chỉ di chuyển theo lệnh hướng (`FORWARD`, `BACKWARD`, `LEFT`, `RIGHT`, `STOP`) gửi từ Web/App.
- **Cơ chế Timeout**: Mỗi lệnh di chuyển có một thời hạn (`durationMs`). Nếu hết thời gian mà không có lệnh mới, xe sẽ tự động dừng (`MOVE_STOP`) để đảm bảo an toàn.

---

## 3. Logic Cảnh báo & Bảo vệ Môi trường
Hệ thống ưu tiên an toàn môi trường lên trên hết:
- Nếu phát hiện **Khói**, **Bụi mịn cao**, hoặc **Độ ẩm cực thấp**, xe sẽ kích hoạt trạng thái báo động (`envAlarm` / `dryAlarm`).
- **Ưu tiên dừng xe**: Ngay khi có báo động, xe sẽ dừng lại ngay lập tức bất kể đang ở chế độ nào.
- **Xử lý tự động**: Kích hoạt Relay 1 (xử lý khí) hoặc Relay 2 (tạo ẩm) để cải thiện chất lượng không khí tại vị trí đó.

---

## 4. Cơ chế Đồng bộ Dữ liệu

### Cloud Sync (TaskAPI)
- Cứ mỗi 2 giây, ESP32 sẽ gọi API backend để kiểm tra xem người dùng có thay đổi cấu hình (Kp, Ki, Kd, Tốc độ, Ngưỡng) hay gửi lệnh mới không.
- **Chống ghi đè**: Hệ thống đã được lập trình để không cho phép cấu hình "Tự động" đè lên khi xe đang ở chế độ "Thủ công", tránh việc xe tự chạy ngoài ý muốn.

### Giao tiếp Sensor (TaskEnv)
- Dữ liệu cảm biến được gửi lên Cloud định kỳ (mỗi 60 giây hoặc khi có thay đổi quan trọng) để người dùng theo dõi lịch sử qua Dashboard.

---

## 5. Bản đồ chân (Pin Mapping) sơ lược
- **Siêu âm**: 16, 17 (Trái); 18, 19 (Giữa); 23, 22 (Phải).
- **Động cơ**: 32, 33, 25 (Trái); 14, 27, 26 (Phải).
- **Cảm biến**: 35 (Khói); 34 (Bụi); 21 (DHT11).
- **Relay**: 13 (Khí); 4 (Ẩm).

---

## 6. Lưu ý vận hành
- Xe ưu tiên kết nối WiFi ở chế độ Trạm (STA).
- Giao diện Web local có thể truy cập qua địa chỉ IP của ESP32 hiển thị trên Serial Monitor.
- Khi chuyển từ AUTO sang MANUAL, xe sẽ tạm dừng một nhịp để reset các thông số PID, đảm bảo chuyển trạng thái an toàn.