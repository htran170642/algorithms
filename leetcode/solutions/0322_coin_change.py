class Solution:
    def coinChange(self, coins: List[int], amount: int) -> int:
        '''
        i = 11 thi coin cuoi cung co the la 1, 2, 5
        dp[11] = dp[10] + 1
        dp[11] = dp[9] + 1
        dp[11] = dp[6] + 1

        dp[i] so luong coin it nhat de tao thanh i
        '''

        dp = [float('inf')] * (amount + 1)

        dp[0] = 0

        for i in range(1, amount + 1):
            for coin in coins:
                if i >= coin:
                    dp[i] = min(
                        dp[i], 
                        dp[i-coin] + 1
                    )

        if dp[amount] == float('inf'):
            return -1

        return dp[amount]