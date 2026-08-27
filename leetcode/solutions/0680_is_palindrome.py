class Solution:
    def validPalindrome(self, s: str) -> bool:
        def is_paindrom(l, r):
            while l < r:
                if s[l] != s[r]:
                    return False

                l += 1
                r -= 1
            return True

        l, r = 0, len(s) -1

        while l < r:
            if s[l] != s[r]:
                return is_paindrom(l+1, r) or is_paindrom(l, r-1)

            l += 1
            r -= 1

        return True