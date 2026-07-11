from typing import List

class Solution:
    def search(self, nums: List[int], target: int) -> int:
        l, r = 0, len(nums) - 1

        while l <= r:
            mid = (r + l) // 2
            if nums[mid] == target:
                return mid

            if nums[mid] < target:
                l = mid + 1
            else:
                r = mid - 1

        return -1

def main():
    solution = Solution()
    nums = [-1, 0, 3, 5, 9, 12]
    target = 9
    result = solution.search(nums, target)
    print(result)  # Output: 4

if __name__ == "__main__":
    main()
