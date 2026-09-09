import sys

def main():
    data = sys.stdin.read().split()
    idx = 0
    n = int(data[idx]); idx += 1
    x = int(data[idx]); idx += 1

    p = [0]*n
    t = [0]*n
    for i in range(n):
        p[i] = int(data[idx]); idx += 1
        t[i] = int(data[idx]); idx += 1

    CAP = 2 * 10**15  # chan so trung moi con ga de tranh so qua lon (khong can thiet o python nhung giu cho gon)

    def count_eggs(T):
        total = 0
        for i in range(n):
            if T >= p[i]:
                c = (T - p[i]) // t[i] + 1
                if c > CAP:
                    c = CAP
                total += c
                if total >= x:  # thoat som cho nhanh
                    return total
        return total

    lo, hi = 1, 2 * 10**18
    while lo < hi:
        mid = (lo + hi) // 2
        if count_eggs(mid) >= x:
            hi = mid
        else:
            lo = mid + 1

    print(lo)

if __name__ == "__main__":
    main()