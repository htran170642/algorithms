from typing import List
class Solution:
    def majorityElement(self, nums: List[int]) -> int:
        n = len(nums)
        major = n // 2

        freq = {}
        for num in nums:
            freq[num] = freq.get(num, 0) + 1

        for k, v in freq.items():
            if v > major:
                return k

def main():
    solution = Solution()
    nums = [3, 2, 3]
    result = solution.majorityElement(nums)
    print(result)  # Output: 3

if __name__ == "__main__":
    main()
