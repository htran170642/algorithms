class Solution:
    def backspaceCompare(self, s: str, t: str) -> bool:
        def next_char(org_s, i):
            skip = 0
            while i >= 0:

                if org_s[i] == "#":
                    skip += 1
                elif skip > 0:
                    skip -= 1
                else:
                    return org_s[i], i - 1

                i -= 1

            return None, -1


        i, j = len(s) - 1, len(t) -1

        while i >= 0 or j >= 0:

            char_s, i = next_char(s, i)
            char_t, j = next_char(t, j)

            if char_s != char_t:
                return False

        return True

                

