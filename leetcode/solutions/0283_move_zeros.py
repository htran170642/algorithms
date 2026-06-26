from typing import List
class Solution:
    def moveZeroes(self, nums: List[int]) -> None:
        """
        Do not return anything, modify nums in-place instead.
        """
        slow = 0

        # fast slow pointer
        for fast in range(len(nums)):
            if nums[fast] != 0:
                nums[slow], nums[fast] = nums[fast], nums[slow]
                slow += 1

        
def main():
    solution = Solution()
    nums = [0,1,0,3,12]
    solution.moveZeroes(nums)
    print(nums)

if __name__ == "__main__":
    main()
