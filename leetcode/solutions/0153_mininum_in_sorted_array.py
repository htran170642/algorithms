class Solution:
    def findMin(self, nums: List[int]) -> int:
        l, r = 0, len(nums) - 1

        ans = float("inf")

        while l < r: # luon giu 1 phan tu nen k dung =
            mid = (r + l) // 2
            if nums[mid] > nums[r]:
                l = mid + 1
            else:
                r = mid # mid co the la minimum

        return nums[l]