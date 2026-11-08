# kubelet: Verify Service & Pod Creation (minikube)

> Prompt `hieptt@hieptt` = máy host · `docker@minikube` = trong node (`minikube ssh`).
> kubelet là **systemd service** → `systemctl` chỉ dùng được **trong node**.

## 1. Verify kubelet Service

```bash
minikube ssh

# Có đang chạy không?
systemctl is-active kubelet
systemctl status kubelet --no-pager

# Service hay Pod? → là systemd service; không có manifest trong staticPodPath
systemctl cat kubelet
ls /etc/kubernetes/manifests/          # chỉ có etcd, apiserver, controller-manager, scheduler

# PID
systemctl show kubelet -p MainPID
pgrep -x kubelet

# User
ps -o pid,user,comm -C kubelet
systemctl show kubelet -p User          # rỗng = root

# Port đang listen
sudo ss -ltnp | grep '"kubelet"'

# Log
sudo journalctl -u kubelet -n 50 --no-pager

exit
```

| Câu hỏi | Kết quả (minikube) |
|---|---|
| kubelet có đang chạy? | Có: `active` |
| Service hay Pod? | **systemd service** (không phải Pod): kubelet là thứ tạo ra static pod nên không thể tự chạy dạng pod |
| PID | `607` (trong node) / `9526` (host) |
| User | `root` (cần quyền quản lý container, mount volume, network) |
| Port | `10250` kubelet API (logs/exec) · `10248` healthz (localhost) |
| Config | `--config=/var/lib/kubelet/config.yaml`, kubeconfig `/etc/kubernetes/kubelet.conf` |

## 2. Verify kubelet Creates Pods

```bash
# Trên host: tạo pod test
kubectl run nginx --image=nginx
kubectl wait --for=condition=Ready pod/nginx --timeout=120s

# Pod chạy trên node nào?
kubectl get pod nginx -o wide

# Ai làm gì? → xem cột SOURCE của events
kubectl get events --field-selector involvedObject.name=nginx --sort-by=.lastTimestamp \
  -o custom-columns=SOURCE:.source.component,REASON:.reason,MSG:.message

# Container runtime
kubectl get node minikube -o jsonpath='{.status.nodeInfo.containerRuntimeVersion}{"\n"}'

# Trong node: runtime có chạy nginx không?
minikube ssh
sudo crictl ps --name nginx
sudo crictl pods --name nginx

# kubelet có nói chuyện với etcd không? → xem kết nối đang mở
sudo ss -tnp | grep '"kubelet"'                 # chỉ tới 192.168.49.2:8443 (apiserver)
sudo grep server: /etc/kubernetes/kubelet.conf  # server: https://192.168.49.2:8443
exit

# Dọn dẹp
kubectl delete pod nginx
```

Events:

```
SOURCE              REASON      MSG
default-scheduler   Scheduled   Successfully assigned default/nginx to minikube
kubelet             Pulling     Pulling image "nginx"
kubelet             Pulled      Successfully pulled image "nginx"
kubelet             Created     Container created
kubelet             Started     Container started
```

| Câu hỏi | Kết quả |
|---|---|
| Pod nginx chạy trên node nào? | `minikube` |
| kubelet hay scheduler tạo container? | **kubelet**. Scheduler chỉ chọn node (`Scheduled`); kubelet pull image, tạo và start container |
| Runtime có chạy nginx? | Có: `crictl ps` → `nginx Running`, runtime `containerd://2.3.4` |
| kubelet giao tiếp trực tiếp etcd? | **Không**: kết nối duy nhất là tới apiserver `:8443`, không có kết nối `:2379` |

## Luồng tạo Pod

```
kubectl run → kube-apiserver → etcd (lưu Pod, nodeName trống)
                   ↑
kube-scheduler ────┘ chọn node, ghi Binding (nodeName=minikube)
                   ↓
kubelet (watch apiserver) thấy Pod của node mình
   → CRI → containerd → pull image, tạo & start container
   → báo status về apiserver
```

## So sánh với control plane

| | kube-apiserver / controller-manager / scheduler | kubelet |
|---|---|---|
| Chạy dạng | Static Pod | systemd service |
| Ai khởi động | kubelet (đọc `/etc/kubernetes/manifests`) | systemd |
| Kiểm tra | `kubectl -n kube-system get pod`, `crictl ps` | `systemctl status kubelet`, `journalctl -u kubelet` |
| Kết nối etcd | Chỉ apiserver | Không |
