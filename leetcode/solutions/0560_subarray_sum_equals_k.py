from typing import List
class Solution:
    def subarraySum(self, nums: List[int], k: int) -> int:
        # old_prefix = current_sum - k

        count = 0
        prefix = 0

        freq = {0: 1}
        for num in nums:
            prefix += num

            count += freq.get(prefix - k, 0)

            freq[prefix] = freq.get(prefix, 0) + 1

        return count

def main():
    solution = Solution()
    nums = [1, 1, 1]
    k = 2
    result = solution.subarraySum(nums, k)
    print(result)  # Output: 2

if __name__ == "__main__":
    main()w