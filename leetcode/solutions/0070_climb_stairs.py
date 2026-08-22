class Solution:
    def climbStairs(self, n: int) -> int:
        # 1
        # 1 1 / 1 2
        # 1 1 1 / 1 +2  / 2 + 1
        # 1 1 1 1/ 1 1 2 / 1 2 1 / 2 1 1/ 2 2

        if n <= 2:
            return n

        dp = [0] * (n + 1)

        dp[1] = 1
        dp[2] = 2

        for i in range(3, n+1):
            dp[i] = dp[i-1] + dp[i-2]

        return dp[n]

        # state la gi, transition la gi, base case, answer dp[n]
        