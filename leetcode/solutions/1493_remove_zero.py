class Solution:
    def longestSubarray(self, nums: list[int]) -> int:
        zero_count = 0

        left = 0
        res = 0

        for right in range(len(nums)):
            if nums[right] == 0:
                zero_count += 1

            while zero_count > 1:
                if nums[left] == 0:
                    zero_count -= 1
                left += 1

            res = max(res, right - left + 1)
        print(res)
        return res - 1