class Solution:
    def wordBreak(self, s: str, wordDict: list[str]) -> bool:
        '''
        dp[i] = true nếu s[:i] là true

        '''

        words = set(wordDict)

        n = len(s)

        dp = [False] * (n+1)
        dp[0] = True

        for i in range(1, n+1):
            for j in range(i):
                if dp[j] and s[j:i] in words:
                    dp[i] = True
                    break

        return dp[n]