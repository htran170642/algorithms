# Sliding Window

## 1. Core Idea

Duy trì một đoạn liên tiếp:

```text
[left ........ right]
```

Thay vì tính lại toàn bộ window, mỗi bước:

```text
add(nums[right])
remove(nums[left])
```

Mục tiêu: thường giảm từ `O(n*k)` → `O(n)`.

---

## 2. Fixed Window

Window có kích thước cố định `k`.

### Template

```python
left = 0
window = 0

for right in range(len(nums)):
    window += nums[right]

    if right - left + 1 > k:
        window -= nums[left]
        left += 1

    if right - left + 1 == k:
        # update answer
```

### Problems

* LC643 — Maximum Average Subarray I
* LC1343 — Number of Sub-arrays of Size K
* LC1456 — Maximum Number of Vowels

---

## 3. Variable Window

Window thay đổi kích thước.

### Core flow

```text
Expand → Invalid → Shrink → Valid → Update
```

### Template

```python
left = 0

for right in range(len(nums)):
    # add right

    while invalid:
        # remove left
        left += 1

    # window is valid
    answer = max(answer, right - left + 1)
```

### Problems

* LC209 — Minimum Size Subarray Sum
* LC713 — Subarray Product Less Than K
* LC904 — Fruit Into Baskets
* LC1004 — Max Consecutive Ones III

---

## 4. Frequency Map Window

Dùng khi window cần theo dõi:

* frequency
* duplicate
* distinct characters
* anagram
* replacement

```python
count = {}

count[x] = count.get(x, 0) + 1

# remove
count[x] -= 1

if count[x] == 0:
    del count[x]
```

### Problems

* LC3 — Longest Substring Without Repeating Characters
* LC424 — Longest Repeating Character Replacement
* LC567 — Permutation in String
* LC438 — Find All Anagrams
* LC76 — Minimum Window Substring

---

## 5. Longest vs Shortest

### Longest Valid Window

```python
for right in range(n):
    add(right)

    while invalid:
        remove(left)
        left += 1

    answer = max(answer, right - left + 1)
```

### Shortest Valid Window

```python
for right in range(n):
    add(right)

    while valid:
        answer = min(answer, right - left + 1)

        remove(left)
        left += 1
```

---

## 6. At Most K

Pattern:

```text
condition <= K
```

Ví dụ:

```text
At most K distinct
At most K zeros
```

Template:

```python
for right in range(n):
    add(right)

    while condition > K:
        remove(left)
        left += 1

    answer = max(answer, right - left + 1)
```

Problems:

* LC904 — At most 2 distinct
* LC340 — At most K distinct
* LC1004 — At most K zeros

---

## 7. Exactly K

Một pattern quan trọng:

```text
exactly(K)
=
atMost(K) - atMost(K - 1)
```

Problems:

* LC930 — Binary Subarrays With Sum
* LC1248 — Count Number of Nice Subarrays
* LC992 — Subarrays with K Different Integers

---

## 8. Monotonic Deque

Dùng khi cần:

```text
max/min trong mỗi window
```

### Problems

* LC239 — Sliding Window Maximum
* LC1438 — Longest Continuous Subarray
* LC862 — Shortest Subarray with Sum at Least K

Pattern:

```text
Window + Monotonic Deque
```

---

## 9. Recognition Cues

Gặp:

```text
substring
subarray
contiguous
consecutive
longest
shortest
maximum/minimum
at most K
exactly K
frequency
distinct
anagram
```

→ nghĩ đến **Sliding Window**.

Nếu cần `max/min` của mỗi window:

```text
→ Monotonic Deque
```

---

## 10. Mental Model

```text
Sliding Window
│
├── Fixed Window
│   └── size = K
│
├── Variable Window
│   └── expand / shrink
│
├── Frequency Map
│   └── frequency / distinct / duplicate
│
├── At Most / Exactly K
│   └── exactly(K) = atMost(K) - atMost(K-1)
│
└── Monotonic Deque
    └── window max / min
```

### Câu cần nhớ

> **Expand bằng `right`, shrink bằng `left`, luôn xác định rõ window valid khi nào.**
