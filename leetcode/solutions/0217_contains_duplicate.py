#https://leetcode.com/problems/contains-duplicate/description/

from typing import List

class Solution:
    def containsDuplicate(self, nums: List[int]) -> bool:
        seen = set()
        for num in nums:
            if num in seen:
                return True
            seen.add(num)

        return False

def main():
    nums = [1, 2, 3, 1]
    solution = Solution()
    result = solution.containsDuplicate(nums)
    print(result)  # Output: True

if __name__ == "__main__":
    main()

# Time: O(n), Space: O(n)