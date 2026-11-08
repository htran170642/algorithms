# kube-scheduler: Verify Process & Static Pod (minikube)

> Prompt `hieptt@hieptt` = máy host · `docker@minikube` = trong node (`minikube ssh`).
> Manifest và kubeconfig chỉ có **trong node**.

## 1. Verify process

```bash
ps -ef | grep kube-scheduler

# PID
pgrep -f 'kube-scheduler --'
# hoặc: ps -ef | grep [k]ube-scheduler | awk '{print $2}'

# Tất cả flag (scheduler chỉ có 5 flag)
ps -ef | grep [k]ube-scheduler | tr ' ' '\n' | grep -- '^--'

# Secure port (mặc định 10259, không có trong flag) → trong node
sudo ss -ltnp | grep kube-sched

# etcd / authorization-mode → không có kết quả
ps -ef | grep [k]ube-scheduler | tr ' ' '\n' | grep -E -- '--(etcd|authorization-mode|tls-cert)'

# Kubeconfig + authn/authz kubeconfig
ps -ef | grep [k]ube-scheduler | tr ' ' '\n' | grep -- kubeconfig

# Leader election
ps -ef | grep [k]ube-scheduler | tr ' ' '\n' | grep -- --leader-elect
```

| Câu hỏi | Kết quả (minikube) |
|---|---|
| PID | `10682` (host) / `1102` (trong node) |
| Secure port | `10259` (mặc định), bind `127.0.0.1` |
| etcd endpoint | **Không có**: chỉ nói chuyện với apiserver |
| Authorization mode | **Không có** `--authorization-mode` (chỉ apiserver có). Scheduler bị phân quyền bằng RBAC với user `system:kube-scheduler`; `--authorization-kubeconfig` để hỏi apiserver khi có request gọi vào port 10259 |
| Kết nối apiserver | `--kubeconfig=/etc/kubernetes/scheduler.conf` → `https://192.168.49.2:8443`, user `system:kube-scheduler` |
| TLS cert | Không có `--tls-cert-file` → port 10259 dùng cert tự sinh (self-signed); client cert gọi apiserver nằm trong `scheduler.conf` |
| Leader election | `--leader-elect=false` → không có Lease |

## 2. Verify static pod

```bash
minikube ssh

sudo cat /etc/kubernetes/manifests/kube-scheduler.yaml

# Static pod? → manifest nằm trong staticPodPath
sudo grep staticPodPath /var/lib/kubelet/config.yaml
ls /etc/kubernetes/manifests/

# Namespace
sudo grep namespace /etc/kubernetes/manifests/kube-scheduler.yaml

# Giá trị --kubeconfig
sudo grep -- --kubeconfig /etc/kubernetes/manifests/kube-scheduler.yaml

# Kubeconfig trỏ tới apiserver nào, user nào
sudo grep -E 'server:|user:' /etc/kubernetes/scheduler.conf

exit
```

| Câu hỏi | Kết quả |
|---|---|
| Có phải Static Pod? | Có: owner `Node`, `config.source=file`, tên `kube-scheduler-minikube` |
| Namespace | `kube-system` |
| File kubelet dùng | `/etc/kubernetes/manifests/kube-scheduler.yaml` |
| `--kubeconfig` | `/etc/kubernetes/scheduler.conf` |

## 3. Xác nhận bằng kubectl (trên host)

```bash
kubectl -n kube-system get pod -l component=kube-scheduler -o wide

P=kube-scheduler-minikube
kubectl -n kube-system get pod $P -o jsonpath='{.metadata.ownerReferences[0].kind}{"\n"}'                       # Node
kubectl -n kube-system get pod $P -o jsonpath='{.metadata.annotations.kubernetes\.io/config\.source}{"\n"}'    # file

# RBAC của scheduler
kubectl get clusterrolebinding system:kube-scheduler -o wide
kubectl auth can-i create pods --subresource=binding --as=system:kube-scheduler   # yes: gán pod vào node
kubectl auth can-i list nodes --as=system:kube-scheduler                          # yes
kubectl auth can-i delete secrets --as=system:kube-scheduler                      # no

# Leader election (chỉ có khi --leader-elect=true)
kubectl -n kube-system get lease kube-scheduler
```

> ⚠️ `kubectl auth can-i create pods/binding` → `no` (sai cú pháp). Subresource phải dùng `--subresource=binding`.

## So sánh control plane

| | kube-apiserver | kube-controller-manager | kube-scheduler |
|---|---|---|---|
| Port | 8443 (minikube) / 6443 | 10257 (localhost) | 10259 (localhost) |
| Kết nối etcd | Có (`--etcd-servers`) | Không | Không |
| `--authorization-mode` | `Node,RBAC` | Không | Không |
| Kubeconfig | — | `controller-manager.conf` | `scheduler.conf` |
| User RBAC | — | `system:kube-controller-manager` | `system:kube-scheduler` |
