## Dấu hiệu

- Đếm số lần xuất hiện
- So sánh 2 chuỗi
- character statistics

## Templates

freq = {}

for item in data:
    freq[item] = freq.get(item, 0) + 1

Build O(n),
Lookup O(1) 


# Expand
count[s[right]] += 1

# ...

# Shrink
count[s[left]] -= 1
left += 1