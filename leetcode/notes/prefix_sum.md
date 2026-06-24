# Prefix Sum + Hash Map

## Dấu hiệu

- Subarray Sum
- Count Subarray
- Sum = K

## Công thức

prefix[r] - prefix[l-1] = k

=>

prefix[l-1] = prefix[r] - k

## Template

prefix += num

answer += freq[prefix - k]

freq[prefix] += 1

## Related Problems

- LC 560
- LC 525
- LC 974
- LC 930
- LC 1248

## Mistakes

- Quên freq = {0:1}
- Update freq trước khi count