class Solution:
    def minCostClimbingStairs(self, cost: list[int]) -> int:
        # dp[i] = min(dp[i-1], dp[i-2]) + cost[i]

        n = len(cost)
        dp = [0] * n
        dp[0] = cost[0]
        dp[1] = cost[1]

        for i in range(2, len(cost)):
            dp[i] = min(dp[i-1], dp[i-2]) + cost[i]

        return min(dp[n-1], dp[n-2])