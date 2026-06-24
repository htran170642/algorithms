from typing import List

class Solution:
    def longestConsecutive(self, nums: List[int]) -> int:
        if not nums:
            return 0

        longest = 0
        num_set = set(nums)
        for num in num_set:
            # count only if `num` is the start of a sequence
            if num - 1 not in num_set:
                length = 1

                while num + length in num_set:
                    length += 1

                longest = max(longest, length)

        return longest

def main():
    solution = Solution()
    nums = [100, 4, 200, 1, 3, 2]
    result = solution.longestConsecutive(nums)
    print(result)  # Output: 4

if __name__ == "__main__":
    main()