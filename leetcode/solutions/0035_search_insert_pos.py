
class Solution:
    def searchInsert(self, nums: List[int], target: int) -> int:
        #return bisect.bisect_left(nums, target)

        l, r = 0, len(nums) - 1

        while l <= r:
            mid = (r + l) // 2
            # continue to shrink
            if nums[mid] < target:
                l = mid + 1
            else:
                r = mid - 1
        
        return l