# DOANTOTNGHIEP
Tên đề tài: Hệ thống điều khiển và tự động cho vườn thanh long
 Tính năng chính
- Điều khiển không dây bằng giao tiếp zigbee
- Đo nhiệt độ và độ ẩm, độ ẩm đất, Thông số N,P,K bằng các cảm biến
- Hiển thị thông tin lên màn hình TFT ILI9341
- Điều khiển chế độ **tự động/thủ công** bằng nút nhấn hoặc bằng app trên điện thoại
- Gửi dữ liệu thời gian thực lên Firebase Realtime Database
- Lưu lịch sử dữ liệu và hiển thị biểu đồ bằng Google Sheets
- Bật tắt đèn chiếu sáng ban đêm hỗ trợ ra hoa thanh long
 Công nghệ sử dụng
- Vi điều khiển: ESP32
- các thiêts bị sử dụng: DHT11, cảm biến độ ẩm đất, cảm biến ánh sáng, cảm biên NPK,máy bơm,đèn, zigbee, màn hình TFT
- Hiển thị: Màn hình TFT 2.4" ILI9341 
- Giao tiếp: Firebase Realtime DB, Google Apps Script
- Phần mềm: Arduino IDE, Firebase Console
