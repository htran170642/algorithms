class Solution:
    def maxVowels(self, s: str, k: int) -> int:
        d = {'a', 'e', 'i', 'o', 'u'}
        
        cnt = 0
        for i in range(k):
            if s[i] in d:
                cnt += 1
        
        ans = cnt

        for i in range(k, len(s)):

            if s[i] in d:
                cnt += 1

            # remove old char
            l = i - k
            if s[l] in d:
                cnt -= 1
            ans = max(ans, cnt)

        return ans