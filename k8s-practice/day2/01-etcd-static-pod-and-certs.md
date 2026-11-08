# etcd: Verify Static Pod & Check Certificates (minikube)

> Lưu ý: minikube (Docker driver) chạy node trong container.
> Manifest, cert và `crictl` nằm **bên trong node** → phải `minikube ssh` trước.
> - Prompt `hieptt@hieptt` = máy host
> - Prompt `docker@minikube` = trong node
>
> Trên cụm kubeadm thật: cert ở `/etc/kubernetes/pki/etcd/` thay vì `/var/lib/minikube/certs/etcd/`.

## 1. Verify etcd Static Pod

```bash
# Trên host
ps -ef | grep etcd
kubectl -n kube-system get pod -l component=etcd -o wide

# Trong node
minikube ssh
sudo crictl ps | grep etcd
sudo cat /etc/kubernetes/manifests/etcd.yaml
sudo ss -ltnp | grep etcd          # xem port đang listen
```

### Kết quả

| Câu hỏi | Trả lời |
|---|---|
| etcd có đang chạy không? | Có: `crictl ps` → `Running`, pod `etcd-minikube` → `1/1 Running` |
| etcd chạy dạng gì? | **Static pod**: manifest ở `/etc/kubernetes/manifests/etcd.yaml`, kubelet tự tạo (không qua API server), tên pod có hậu tố node `etcd-minikube`, `hostNetwork: true` |
| etcd listen port nào? | `2379` client API (127.0.0.1 + 192.168.49.2) · `2380` peer · `2381` metrics/health probe (HTTP, localhost) |
| etcd dùng cert nào? | Trong `/var/lib/minikube/certs/etcd/` (xem bảng dưới) |

| Flag | File | Dùng cho |
|---|---|---|
| `--cert-file` / `--key-file` | `server.crt` / `server.key` | TLS client (2379) |
| `--peer-cert-file` / `--peer-key-file` | `peer.crt` / `peer.key` | TLS peer (2380) |
| `--trusted-ca-file`, `--peer-trusted-ca-file` | `ca.crt` | CA verify cert |

`--client-cert-auth=true` → client bắt buộc có cert do CA ký.
kube-apiserver kết nối etcd bằng `apiserver-etcd-client.crt/.key` (`--etcd-certfile`, `--etcd-keyfile`).

## 2. Check certificate

### Cách 1: vào node rồi chạy

```bash
minikube ssh
cd /var/lib/minikube/certs/etcd
ls -l

# Xem subject, issuer, ngày hết hạn, SAN, key usage
sudo openssl x509 -in server.crt -noout -subject -issuer -dates -ext subjectAltName,extendedKeyUsage
# Xem đầy đủ mọi trường
sudo openssl x509 -in server.crt -noout -text

# Verify cert có được CA ký không
sudo openssl verify -CAfile ca.crt server.crt peer.crt ../apiserver-etcd-client.crt

# Kết nối thử bằng etcdctl (chạy trong container etcd)
D=/var/lib/minikube/certs/etcd
ID=$(sudo crictl ps -q --name etcd)
sudo crictl exec $ID etcdctl --endpoints=https://127.0.0.1:2379 \
  --cacert=$D/ca.crt --cert=$D/healthcheck-client.crt --key=$D/healthcheck-client.key \
  endpoint health -w table

exit
```

### Cách 2: chạy từ host, không vào node (dùng đường dẫn tuyệt đối)

```bash
minikube ssh -- sudo openssl x509 -in /var/lib/minikube/certs/etcd/server.crt -noout -subject -issuer -dates
```

### Cần để ý gì khi check cert

- **dates (notAfter)**: cert hết hạn là nguyên nhân hay gặp khiến control plane chết. Server/peer cert: 1 năm, CA: 10 năm.
- **SAN**: phải chứa IP/hostname mà client dùng để kết nối (`127.0.0.1`, `192.168.49.2`, `localhost`, `minikube`).
- **Issuer**: phải là `etcd-ca`; `openssl verify` → `OK`.
- **Không có client cert** → `etcdctl` báo `unhealthy cluster`, vì `--client-cert-auth=true`.

### Xem hạn tất cả cert control plane: `kubeadm certs check-expiration`

```bash
# Cụm kubeadm thật
sudo kubeadm certs check-expiration
sudo kubeadm certs renew all           # gia hạn, sau đó restart static pod control plane

# minikube: kubeadm không có trong PATH, cert ở /var/lib/minikube/certs → phải trỏ config
minikube ssh
sudo /var/lib/minikube/binaries/v1.37.0/kubeadm certs check-expiration --config /var/tmp/minikube/kubeadm.yaml
```

Kết quả (minikube):

| Cert | Hết hạn | Còn lại | CA ký |
|---|---|---|---|
| `etcd-server`, `etcd-peer`, `etcd-healthcheck-client`, `apiserver-etcd-client` | 2027-10-13 | 363d | `etcd-ca` |
| `admin.conf`, `apiserver-kubelet-client`, `controller-manager.conf`, `scheduler.conf`, `super-admin.conf` | 2027-10-13 | 363d | `ca` |
| `front-proxy-client` | 2027-10-13 | 363d | `front-proxy-ca` |
| `apiserver` | 2029-10-12 | ~3y | `ca` |
| CA: `ca`, `etcd-ca`, `front-proxy-ca` | 2036-10-10 | 9y | (tự ký) |

→ etcd có CA riêng (`etcd-ca`), tách biệt với `ca` của cluster.

## Lỗi hay gặp

| Lỗi | Nguyên nhân | Cách sửa |
|---|---|---|
| `Could not open file ... server.crt` / `No such file or directory` | Đang ở host hoặc sai thư mục | `minikube ssh` rồi `cd /var/lib/minikube/certs/etcd`, hoặc dùng đường dẫn tuyệt đối |
| `sudo: crictl: command not found` | `crictl` không có trên host | Chạy trong `minikube ssh` |
