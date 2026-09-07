class Solution:
    def wordPattern(self, pattern: str, s: str) -> bool:
        p_2_s = {}
        s_2_p = {}

        s = s.split(" ")
        print(s)

        if len(pattern) != len(s):
            return False
        
        for i in range(len(s)):
            a = pattern[i]
            b = s[i]

            if a in p_2_s and b != p_2_s[a]:
                return False

            if b in s_2_p and a != s_2_p[b]:
                return False

            p_2_s[a] = b
            s_2_p[b] = a

        return True