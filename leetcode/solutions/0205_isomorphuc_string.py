class Solution:
    def isIsomorphic(self, s: str, t: str) -> bool:
        # egg add
        # badc baba

        s_2_t = {}
        t_2_s = {}

        for i in range(len(s)):
            a = s[i]
            b = t[i]

            if a in s_2_t and s_2_t[a] != b:
                return False

            if b in t_2_s and t_2_s[b] != a:
                return False

            s_2_t[a] = b
            t_2_s[b] = a
            
        return True