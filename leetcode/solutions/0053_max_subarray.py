class Solution:
    def maxSubArray(self, nums: list[int]) -> int:

        '''
        2 options:
        option 1: nối vào subarray cũ
        dp[i-1] + nums[i]

        option 2:
        bắt đầu sắp array mới tại nums[i]
        '''
        n = len(nums)

        dp = [0] * n
        dp[0] = nums[0]
        res = dp[0]

        for i in range(1, n):
            dp[i] = max(nums[i], nums[i] + dp[i-1])
            res = max(res, dp[i])
        return res

