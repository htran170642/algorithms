# Install etcd

## 1. Prebuilt binary (recommended)

```bash
ETCD_VER=v3.6.5   # check https://github.com/etcd-io/etcd/releases for the latest
DOWNLOAD_URL=https://github.com/etcd-io/etcd/releases/download

curl -L ${DOWNLOAD_URL}/${ETCD_VER}/etcd-${ETCD_VER}-linux-amd64.tar.gz -o /tmp/etcd.tar.gz
mkdir -p /tmp/etcd && tar xzf /tmp/etcd.tar.gz -C /tmp/etcd --strip-components=1
sudo mv /tmp/etcd/etcd /tmp/etcd/etcdctl /tmp/etcd/etcdutl /usr/local/bin/

etcd --version
etcdctl version
```

## 2. Package manager (simplest, often older version)

```bash
sudo apt install etcd-server etcd-client   # Ubuntu/Debian
```

## 3. Docker

```bash
docker run -d --name etcd -p 2379:2379 -p 2380:2380 \
  quay.io/coreos/etcd:v3.6.5 \
  etcd --name node1 \
    --advertise-client-urls http://0.0.0.0:2379 \
    --listen-client-urls http://0.0.0.0:2379
```

## Quick test

```bash
etcd &                               # single node on localhost:2379
etcdctl put foo bar
etcdctl get foo                      # -> foo / bar
etcdctl endpoint health
```

## Notes for Kubernetes

- With kubeadm / kind / minikube, etcd already runs as a static pod in `kube-system`:
  ```bash
  kubectl -n kube-system get pods | grep etcd
  ```
- Install it manually only to learn etcd on its own, for "Kubernetes the Hard Way", or for an external etcd cluster.
- Ports: `2379` = client API, `2380` = peer (cluster) communication.
