class Solution:
    def removeDuplicates(self, nums: list[int]) -> int:
        k = 0
        # nums = [1,1,1,2,2,3] 
        for num in nums:
            if k < 2 or num != nums[k - 2]:
                nums[k] = num
                k += 1
        return k
