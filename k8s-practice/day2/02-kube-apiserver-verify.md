# kube-apiserver: Verify Process & Static Pod (minikube)

> Prompt `hieptt@hieptt` = máy host · `docker@minikube` = trong node (`minikube ssh`).
> Manifest và kubelet config chỉ có **trong node**.

## 1. Verify process

```bash
ps -ef | grep kube-apiserver

# PID
pgrep kube-apiserver
# hoặc: ps -ef | grep [k]ube-apiserver | awk '{print $2}'

# Secure port
ps -ef | grep [k]ube-apiserver | tr ' ' '\n' | grep -- --secure-port

# etcd endpoint
ps -ef | grep [k]ube-apiserver | tr ' ' '\n' | grep -- --etcd-servers

# Authorization mode
ps -ef | grep [k]ube-apiserver | tr ' ' '\n' | grep -- --authorization-mode

# TLS certificate file
ps -ef | grep [k]ube-apiserver | tr ' ' '\n' | grep -- --tls-

# Gộp tất cả
ps -ef | grep [k]ube-apiserver | tr ' ' '\n' | grep -E -- '--(secure-port|etcd-servers|authorization-mode|tls-)'
```

| Câu hỏi | Kết quả (minikube) |
|---|---|
| PID | `10679` (host) / `1099` (trong node). Khác nhau do node minikube có PID namespace riêng |
| Secure port | `8443` (kubeadm thật mặc định `6443`) |
| etcd endpoint | `https://127.0.0.1:2379` |
| Authorization mode | `Node,RBAC` |
| TLS cert | `/var/lib/minikube/certs/apiserver.crt` (key: `apiserver.key`) |

## 2. Verify static pod

```bash
minikube ssh

sudo cat /etc/kubernetes/manifests/kube-apiserver.yaml

# Có phải static pod? → manifest nằm trong staticPodPath của kubelet
sudo grep staticPodPath /var/lib/kubelet/config.yaml
ls /etc/kubernetes/manifests/

# Namespace
sudo grep namespace /etc/kubernetes/manifests/kube-apiserver.yaml

# File kubelet dùng để tạo pod
ls -l /etc/kubernetes/manifests/kube-apiserver.yaml

# Giá trị --etcd-servers
sudo grep -- --etcd-servers /etc/kubernetes/manifests/kube-apiserver.yaml

exit
```

| Câu hỏi | Kết quả |
|---|---|
| Có phải Static Pod? | Có: manifest trong `staticPodPath: /etc/kubernetes/manifests`, tên pod có hậu tố node `kube-apiserver-minikube`, owner là `Node`, `config.source=file` |
| Namespace | `kube-system` |
| File kubelet dùng | `/etc/kubernetes/manifests/kube-apiserver.yaml` |
| `--etcd-servers` | `https://127.0.0.1:2379` |

## 3. Xác nhận thêm bằng kubectl (trên host)

```bash
kubectl -n kube-system get pod -l component=kube-apiserver -o wide

# Owner = Node → static pod (pod thường có owner ReplicaSet/DaemonSet...)
kubectl -n kube-system get pod kube-apiserver-minikube -o jsonpath='{.metadata.ownerReferences[0].kind}{"\n"}'

# config.source = file → kubelet tạo từ file manifest
kubectl -n kube-system get pod kube-apiserver-minikube -o jsonpath='{.metadata.annotations.kubernetes\.io/config\.source}{"\n"}'
```

## Mẹo

- `grep [k]ube-apiserver`: loại bỏ chính dòng `grep` khỏi kết quả.
- `tr ' ' '\n'`: tách command line dài thành từng flag một dòng để grep.
- `grep -- --flag`: `--` báo cho grep đây là pattern, không phải option.
- Liên kết với etcd: apiserver dùng `--etcd-cafile`, `--etcd-certfile`, `--etcd-keyfile` (`etcd/ca.crt`, `apiserver-etcd-client.crt/.key`) để kết nối etcd vì etcd bật `--client-cert-auth=true`.
