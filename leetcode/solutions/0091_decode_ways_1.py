class Solution:
    def numDecodings(self, s: str) -> int:
        '''
        dp[i] số cách giãi mã ký tự i đầu tiên

        s = 226
        dp[0] = 1

        b1: xử lí 2
        dp[1] dp[0] = 1

        b2: xử lý 22

        cách 1: tách ra
        2 2

        cách 2: ghép
        22 -> V

        dp[2] = dp[1] + dp[0]


        b3: 
        22 6
        dp[2] có 2 cách

        2 26
        dp[1] có 1 cách
        dp[3]=dp[2]+dp[1]=2+1=3

        '''

        n = len(s)

        dp = [0] * (n+1) # decode first i characters

        dp[0] = 1

        for i in range(1, n+1):
            if s[i-1] != '0':
                dp[i] += dp[i - 1]

            if i >= 2:
                two_digit = int(s[i-2:i])

                if 10 <= two_digit <= 26:
                    dp[i] += dp[i - 2]

        return dp[n]