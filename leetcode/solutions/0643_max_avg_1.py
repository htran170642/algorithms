class Solution:
    def findMaxAverage(self, nums: list[int], k: int) -> float:
        window = sum(nums[:k])
        ans = window
        # 0,1,2,3,4,5 , k = 4
        for right in range(k, len(nums)):
            window = window - nums[right - k] + nums[right]

            ans = max(ans, window)

        return ans / k