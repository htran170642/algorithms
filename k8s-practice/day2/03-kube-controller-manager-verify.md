# kube-controller-manager: Verify Process & Static Pod (minikube)

> Prompt `hieptt@hieptt` = máy host · `docker@minikube` = trong node (`minikube ssh`).
> Manifest và kubeconfig chỉ có **trong node**.

## 1. Verify process

```bash
ps -ef | grep kube-controller-manager

# PID — dùng -f để khớp full command line
pgrep -f kube-controller-manager
# hoặc: ps -ef | grep [k]ube-controller-manager | awk '{print $2}'

# Secure port (mặc định 10257, không có trong flag) → xem port đang listen (trong node)
sudo ss -ltnp | grep kube-controller
ps -ef | grep [k]ube-controller-manager | tr ' ' '\n' | grep -E -- '--(bind-address|secure-port)'

# Kubeconfig dùng để kết nối apiserver
ps -ef | grep [k]ube-controller-manager | tr ' ' '\n' | grep -- --kubeconfig

# Controllers được bật
ps -ef | grep [k]ube-controller-manager | tr ' ' '\n' | grep -- --controllers

# Leader election
ps -ef | grep [k]ube-controller-manager | tr ' ' '\n' | grep -- --leader-elect

# Cert/key ký CSR, key ký ServiceAccount token
ps -ef | grep [k]ube-controller-manager | tr ' ' '\n' | grep -E -- '--(cluster-signing|service-account-private-key|root-ca)'

# Pod CIDR / Service CIDR
ps -ef | grep [k]ube-controller-manager | tr ' ' '\n' | grep -E -- '--(cluster-cidr|service-cluster-ip-range|allocate-node-cidrs)'
```

| Câu hỏi | Kết quả (minikube) |
|---|---|
| PID | `10667` (host) / `1087` (trong node) |
| Secure port | `10257` (mặc định), bind `127.0.0.1` |
| Kết nối apiserver | `--kubeconfig=/etc/kubernetes/controller-manager.conf` → `https://192.168.49.2:8443`, user `system:kube-controller-manager` |
| Controllers | `--controllers=*,bootstrapsigner,tokencleaner` |
| Leader election | `--leader-elect=false` (1 node) → không có Lease |
| Ký CSR | `--cluster-signing-cert-file=/var/lib/minikube/certs/ca.crt`, `--cluster-signing-key-file=.../ca.key` |
| Ký SA token | `--service-account-private-key-file=.../sa.key` (apiserver dùng `sa.pub` để verify) |
| Pod CIDR / Service CIDR | `10.244.0.0/16` / `10.96.0.0/12` |

> ⚠️ `pgrep kube-controller` ra **2 PID**: process thứ 2 là `/usr/bin/kube-controllers` của **Calico** (CNI).
> Lý do: tên process (comm) bị cắt còn 15 ký tự → cả 2 đều là `kube-controller`. Dùng `pgrep -f kube-controller-manager`.
> Kiểm tra: `ps -o pid,comm,args -p <PID>`

## 2. Verify static pod

```bash
minikube ssh

sudo cat /etc/kubernetes/manifests/kube-controller-manager.yaml

# Static pod? → manifest nằm trong staticPodPath
sudo grep staticPodPath /var/lib/kubelet/config.yaml
ls /etc/kubernetes/manifests/

# Namespace
sudo grep namespace /etc/kubernetes/manifests/kube-controller-manager.yaml

# Giá trị --kubeconfig
sudo grep -- --kubeconfig /etc/kubernetes/manifests/kube-controller-manager.yaml

# Kubeconfig trỏ tới apiserver nào, user nào
sudo grep -E 'server:|user:' /etc/kubernetes/controller-manager.conf

exit
```

| Câu hỏi | Kết quả |
|---|---|
| Có phải Static Pod? | Có: owner `Node`, `config.source=file`, tên `kube-controller-manager-minikube` |
| Namespace | `kube-system` |
| File kubelet dùng | `/etc/kubernetes/manifests/kube-controller-manager.yaml` |
| `--kubeconfig` | `/etc/kubernetes/controller-manager.conf` |

## 3. Xác nhận bằng kubectl (trên host)

```bash
kubectl -n kube-system get pod -l component=kube-controller-manager -o wide

P=kube-controller-manager-minikube
kubectl -n kube-system get pod $P -o jsonpath='{.metadata.ownerReferences[0].kind}{"\n"}'                       # Node
kubectl -n kube-system get pod $P -o jsonpath='{.metadata.annotations.kubernetes\.io/config\.source}{"\n"}'    # file

# Leader election (chỉ có khi --leader-elect=true, vd cụm HA)
kubectl -n kube-system get lease kube-controller-manager -o jsonpath='{.spec.holderIdentity}{"\n"}'
```

## So sánh với kube-apiserver

| | kube-apiserver | kube-controller-manager |
|---|---|---|
| Port | 8443 (minikube) / 6443 (kubeadm) | 10257, chỉ localhost |
| Kết nối etcd? | Có (`--etcd-servers`) | **Không**, chỉ nói chuyện với apiserver qua kubeconfig |
| SA key | `sa.pub` (verify) | `sa.key` (ký) |
