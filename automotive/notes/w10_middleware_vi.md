# Tuần 10 — Kiến trúc middleware: một ranh giới, viết một lần

## 1. Khái niệm

Tuần 8 và 9 làm ra một wire format, một service, và một cách tìm ra nó — dưới
dạng hai chương trình, mỗi cái tự làm mọi thứ bằng tay.

```text
                        tuần 9       tuần 10
svc_client/main.cpp     711 dòng     216
svc_server/main.cpp     492 dòng     139
libs/av_mw              —            1952   dùng chung, 10 integration test
```

Nói thẳng về bảng này: **tổng số dòng code tăng lên.** Mục đích không phải là
bớt code. Mục đích là phần khó giờ chỉ tồn tại **một lần**, có test, và mọi
process sau này — cluster, IVI, HUD, logger — được dùng miễn phí. Bug loopback
của tuần 9 mà bị chép vào năm chương trình thì thành năm bug.

Middleware là **ranh giới** giữa việc ứng dụng *muốn nói gì* và việc byte
*đi thế nào*. Cả tuần xoay quanh một câu hỏi: cái gì được đi qua ranh giới đó.

```text
ứng dụng           vehicle->get_speed(callback)           float và callback
════════════════════ ranh giới ═════════════════════════════════════════════
lớp sinh tự động   vehicle.hpp: method id, float <-> 4 byte
middleware         Runtime · Proxy · Skeleton             SD, lease, session, timeout
wire format        av_service: byte SOME/IP + SD
transport          av_eth: UDP, multicast
```

## 2. Vì sao Cockpit DC cần nó

- **Nhiều client, cùng một service.** Cluster, IVI, HUD, logger, chẩn đoán —
  ai cũng muốn tốc độ xe. Mỗi cái 700 dòng là 700 dòng bug giống nhau, sửa
  riêng từng chỗ, hoặc không bao giờ sửa.
- **Nhiều team.** Team HMI không cần biết lease là gì. Ranh giới này cũng là
  ranh giới tổ chức — `ara::com` của AUTOSAR Adaptive có đúng hình dạng này:
  Proxy và Skeleton sinh ra từ ARXML.
- **Xử lý lỗi tự nhiên chia làm hai.** CLAUDE.md §7 yêu cầu
  `Fault → Detection → Logging → Fallback → Safe State`. *Detection* và
  *logging* nằm **dưới** ranh giới (các dòng log `mw`). *Fallback* nằm **trên**:
  chỉ ứng dụng biết "mất tốc độ" nghĩa là làm mờ kim hay bật cảnh báo.

## 3. Kiến trúc — các quyết định

| Câu hỏi | Quyết định | Vì sao | Cái giá |
|---|---|---|---|
| Ai sở hữu thread? | **Không ai.** `poll()` chạy trên thread người gọi; `fd()` cho vòng lặp của người khác | Qt (tuần 11–12) chỉ cho một UI thread; `QSocketNotifier` canh `fd()` | callback chậm làm kẹt mạng của cả process — đúng luật Qt vốn có |
| Lệnh gọi kết thúc thế nào? | **Đúng một lần**, một trong bảy kiểu, **không bao giờ bên trong `call()`** | code người gọi đúng dù viết theo thứ tự nào | runtime phải có hàng đợi completion |
| Có retry method không? | **Không bao giờ.** FindService: luôn luôn | `UnlockDoors()` mất *reply* là lệnh đã chạy rồi | ứng dụng tự quyết |
| Quá nhiều lệnh gọi? | **Từ chối** bằng `Busy` khi quá 16 | xếp hàng request tới service đã chết chỉ làm tin xấu đến muộn | người gọi phải xử lý `Busy` |
| Service bị rút? | lệnh đang chờ hỏng **ngay** với `NotAvailable` | `Timeout` vừa đến muộn *vừa* nói sai nguyên nhân | — |
| Ownership | Runtime sống lâu hơn Proxy/Skeleton; destructor của Skeleton = StopOffer | destructor không thể quên, và chạy trên mọi đường return sớm | thứ tự khai báo có ý nghĩa |

Một lần `poll()` làm gì, theo thứ tự:

```text
poll(timeout)
  1. epoll_wait       dispatch: SD socket, method socket, event socket
  2. expire lease     lease hết hạn -> proxy fail lệnh đang chờ, rời group
  3. lặp FindService  hỏi lại những gì chưa tìm thấy
  4. lặp OfferService offer lại mỗi chu kỳ
  5. tick             proxy kiểm tra timeout của lệnh gọi
  6. việc đã post     completion đã quyết từ trước (NotAvailable, Busy)
```

Lease trước lệnh gọi, để service vừa biến mất không bị gọi lại trong cùng một
vòng.

**So sánh thẳng thắn.** vsomeip chọn ngược lại ở cả hai điểm: nó có thread
riêng, và chạy **một** routing manager cho mỗi máy mà mọi process nói chuyện
cục bộ. Cả hai đều bảo vệ được. Có thread riêng là đúng cho process không có
event loop của mình; còn routing manager giải quyết đúng vấn đề mà tuần này
*đo* ra được (§4).

## 4. Hiện thực C++ / Linux

### Ranh giới do build ép, không do quy ước

Runtime dùng pimpl, còn state của Proxy và Skeleton nằm trong file `.cpp`.
Không header nào của `av_mw` nhắc tới socket, nên:

```cmake
target_link_libraries(av_mw PUBLIC av_service PRIVATE av_eth av_ipc ...)
target_link_libraries(svc_client PRIVATE av_mw av_log)   # trước: av_service av_eth av_ipc
```

Giờ `#include "av/eth/udp_socket.hpp"` trong app sẽ **không biên dịch được**.
Compiler review ranh giới thay người, và nó không biết mệt.

### Không bao giờ gọi callback bên trong `call()`

```cpp
proxy.call(..., [&] { waiting = false; });
waiting = true;
```

Nếu một lỗi biết ngay (chưa discover, quá bận) chạy callback ngay lập tức, thì
`waiting` bị xoá *trước khi* được đặt — và kẹt ở `true` mãi mãi. Vì vậy mọi
completion, kể cả cái biết ngay, đều được đẩy sang lần `poll()` sau. Test:
`ACallBeforeDiscoveryFailsLocallyAndNeverInsideCall`.

### Đúng một lần: lấy khỏi bảng trước khi chạy handler

```cpp
Pending call = std::move(found->second);
pending.erase(found);      // trước
call.handler(result);      // có thể gọi tiếp, hoặc huỷ luôn proxy
```

Reply đến muộn cho một lệnh đã timeout sẽ không tìm thấy gì trong bảng và bị bỏ
— đó chính là công dụng của session id.

### Callback được phép huỷ chính proxy của nó

Proxy giữ `shared_ptr<State>`; mọi handler mà runtime giữ đều là `weak_ptr`.
Lúc dispatch, weak được lock, nên state còn sống tới khi callback trả về, kể cả
khi callback đã huỷ Proxy. Và bộ dispatch **copy** từng `std::function` trước
khi gọi, vì chủ của nó có thể huỷ đăng ký — tức là huỷ chính cái function đó —
ngay bên trong lời gọi. Iterator invalidation của tuần 1, gặp lại trong event
loop. Test: `AProxyMayBeDestroyedFromInsideItsOwnCallback`, chạy dưới ASan.

### Vì sao không tự retry

Middleware chỉ thấy byte và method id. Nó không thể biết `GetSpeed` gọi lặp
được còn `UnlockDoors` thì không. Timeout sau khi *mất reply* nghĩa là lệnh đã
chạy; retry là chạy hai lần. **Idempotency quyết định có retry hay không, và chỉ
ứng dụng biết điều đó.** FindService là ngoại lệ vì hỏi lại lúc nào cũng vô hại.

### Tìm ra nhờ đo: unicast tới port dùng chung

```text
hai socket, SO_REUSEADDR, cùng port, cùng máy
  A (bind trước)    nhận ['multi']
  B (bind sau)      nhận ['uni0', 'uni1', 'uni2', 'multi']
```

Multicast tới cả hai; **unicast chỉ tới socket bind sau cùng.** Server tuần 9
trả lời FindService bằng unicast tới `127.0.0.1:30490` — và mỗi khi server khởi
động sau, lời đáp đó **quay về chính server**. Client thực ra chỉ học được qua
offer multicast định kỳ, đến nhanh tới mức không dòng log nào lộ ra khác biệt.
av_mw trả lời FindService bằng multicast. Trên xe mỗi ECU có địa chỉ riêng nên
unicast không sao; trên một máy, đây chính là lý do vsomeip đặt **một** routing
manager đứng trước mọi process.

### Thay đổi hành vi có chủ ý

- **Lệch version** giờ bị *server* từ chối bằng `E_WRONG_INTERFACE_VERSION`.
  Server tuần 8 vẫn trả lời rồi để client tự từ chối. Bên nào biết version của
  mình thì bên đó lên tiếng.
- **Session id không còn hiện** trong output của app. Giờ nó thuộc về
  middleware, và đó chính là mục đích.

## 5. Bài thực hành

Vẫn các lệnh như tuần 9 — mọi cờ đều còn chạy.

```bash
./build/debug/apps/svc_client/svc_client      # terminal 1, chạy TRƯỚC
./build/debug/apps/svc_server/svc_server      # terminal 2, vài giây sau
```

Output của client có **hai giọng**, và phân biệt được chúng chính là bài tập:

```text
[cli] <-- NOT_AVAILABLE   (not discovered yet, or withdrawn -- nothing was sent)
INFO  mw  service available service=4660 instance=1
INFO  mw  joined the event group group=239.10.0.2 port=30510
[cli] +++ AVAILABLE    -- calls will now be sent
      ... response và event ...
INFO  mw  StopOffer -- the service was withdrawn on purpose            <- Ctrl-C
INFO  mw  left the event group group=239.10.0.2 port=30510
[cli] !!! UNAVAILABLE  -- calls fail locally until it returns
WARN  mw  peer rebooted -- its session ids went backwards was=5 now=1  <- bật lại
[cli] +++ AVAILABLE    -- calls will now be sent
WARN  mw  call timed out -- lost request, lost reply, or a wedged server  <- kill -9
[cli] <-- TIMEOUT   (sent, no answer: lost datagram or a wedged server)
WARN  mw  lease expired -- nobody said goodbye
[cli] !!! UNAVAILABLE  -- calls fail locally until it returns
```

Dòng `mw` nói **vì sao** — cho người debug mạng. Dòng `[cli]` nói **nghĩa là
gì** — cho người vẽ màn hình. Cách chia đó **chính là** ranh giới.

```bash
svc_client --method 0x0009                     # <-- ERROR E_UNKNOWN_METHOD
svc_server --wrong-version  +  svc_client      # <-- ERROR E_WRONG_INTERFACE_VERSION
svc_server --no-sd          +  svc_client --static   # chạy được: cả hai tin hằng số
svc_server --no-sd          +  svc_client      # NOT_AVAILABLE mãi, không gửi gì
```

## 6. Bài gây lỗi / gỡ lỗi

| Lỗi | Cách tạo | Ứng dụng thấy | Log `mw` nói |
|---|---|---|---|
| Không offer | `svc_server --no-sd` | `NOT_AVAILABLE`, không gửi gì | FindService (ở mức debug) |
| Tắt sạch | Ctrl-C server | `UNAVAILABLE` ngay, 0 timeout | `StopOffer -- withdrawn on purpose` |
| Crash | `kill -9` server | 2 × `TIMEOUT`, rồi `UNAVAILABLE` | `call timed out` ×2, `lease expired` |
| Khởi động lại | tắt, bật | `AVAILABLE` | `peer rebooted ... was=5 now=1` |
| Thiếu method | `--method 0x0009` | `ERROR E_UNKNOWN_METHOD` | server: `no handler for this method` |
| Sai version | `--wrong-version` | `ERROR E_WRONG_INTERFACE_VERSION` | server: `caller speaks another interface version` |
| Quá nhiều lệnh gọi | test `BeyondMaxPending...` | `BUSY`, không gửi gì | — |

Phá một luật và xem test bắt được:

1. Trong `Proxy::State::post_failure`, gọi thẳng `handler(...)` thay vì post.
   Test `ACallBeforeDiscoveryFailsLocallyAndNeverInsideCall` hỏng ở
   `EXPECT_FALSE(completed)`.
2. Trong `Proxy::State::take`, gọi handler *trước khi* xoá khỏi `pending`.
   Test `AProxyMayBeDestroyedFromInsideItsOwnCallback` sẽ erase qua một
   iterator mà callback đã làm hỏng — chạy preset `asan` và đọc báo cáo.

## 7. Câu hỏi phỏng vấn senior

1. Cái gì thuộc về mỗi bên của ranh giới middleware? Detection và fallback nằm
   ở đâu?
2. Middleware có nên sở hữu thread không? Lập luận cả hai phía, và nói vsomeip
   đã chọn gì.
3. Một runtime không có thread gắn vào event loop của Qt thế nào? Cái gì vẫn
   phải chạy khi không có gì đến?
4. Vì sao completion không bao giờ được chạy bên trong lời gọi đã khởi động nó?
5. Làm sao đảm bảo một completion handler chạy đúng một lần?
6. Middleware có nên retry method bị lỗi không? Tính chất nào quyết định?
7. Khi backpressure: xếp hàng hay từ chối? Bảo vệ lựa chọn.
8. Service biến mất khi còn năm lệnh gọi đang chờ. Mỗi người gọi nên thấy gì,
   và khi nào?
9. Làm sao để một callback huỷ an toàn chính đối tượng đã gọi nó?
10. Làm sao để ranh giới API do build ép chứ không do review?
11. Vì sao Skeleton không offer khi được tạo, nhưng lại rút offer khi bị huỷ?
12. Vì sao lớp sinh tự động mỏng, và nhờ vậy làm được điều gì?

## 8. Checklist ôn tập

- [ ] Vẽ được năm tầng và nói được cái gì đi qua ranh giới.
- [ ] Bảo vệ được "không có thread" và nói được cái giá duy nhất của nó.
- [ ] Liệt kê được bảy kiểu kết thúc lệnh gọi, và hai kiểu nào không gửi gì.
- [ ] Giải thích được bug `waiting = true` mà luật số 2 ngăn.
- [ ] Giải thích được vì sao middleware không bao giờ retry method.
- [ ] Giải thích được `take()` trước handler và `weak_ptr` trong handler.
- [ ] Giải thích được vì sao app không còn include được header socket.
- [ ] Giải thích được phát hiện unicast + SO_REUSEADDR và hệ quả của nó.
- [ ] Đã chạy demo vòng đời và khớp được mỗi dòng `mw` với nguyên nhân của nó.

---

**Còn treo cuối tuần 10.**

- **Runtime đơn luồng theo hợp đồng, không phải theo lock.** Gọi nó từ hai
  thread là data race, và TSan sẽ báo. Đó là hệ quả có chủ ý của "không có
  thread", và là điều tuần 12 phải tôn trọng khi Qt bước vào.
- **SubscribeEventgroup / Ack** — vẫn parse, vẫn chưa bao giờ gửi; mang từ tuần
  9 sang.
- **`vsomeip`** vẫn hoãn. Thiết kế tuần này giờ đủ cụ thể để so từng dòng với
  nó — routing manager, thread pool, đủ cả.
- **Tích hợp qua `fd()` chưa được test** cho tới khi có một event loop thật
  điều khiển nó. Đó là tuần 12.

**Tiếp theo — tuần 11:** Qt core — QObject, signal/slot, event loop, và một demo
chặn event loop cho UI đơ cứng. Luật tuần này đặt cho callback — không bao giờ
block — sắp gặp chính cái framework đã làm nó nổi tiếng.
