#https://leetcode.com/problems/two-sum/

from typing import List

class Solution:
    def twoSum(self, nums: List[int], target: int) -> List[int]:
        
        seen = {}

        for i, val in enumerate(nums):
            need = target - val
            if need in seen:
                return [seen[need], i]
            seen[val] = i

def main():
    nums = [2, 7, 11, 15]
    target = 9
    solution = Solution()
    result = solution.twoSum(nums, target)
    print(result)  # Output: [0, 1]

if __name__ == "__main__":
    main()