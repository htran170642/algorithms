# Opposite Direction Two Pointers

## Dấu hiệu

- Palindrome
- Compare both ends
- Reverse without reversing
- Sorted array
- Pair sum

## Template

left = 0
right = n - 1

while left < right:
    if ...
        left += 1
    elif ...
        right -= 1
    else:
        left += 1
        right -= 1

## Complexity

Time: O(n)
Space: O(1)

## Related Problems

- LC 125
- LC 167
- LC 11
- LC 15
- LC 42