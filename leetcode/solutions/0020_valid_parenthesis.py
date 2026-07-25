class Solution:
    def isValid(self, s: str) -> bool:
        dt = {
            ")" : "(",
            "}" : "{",
            "]" : "["
        }

        st = []

        for c in s:
            if c in dt:
                if not st:
                    return False
                if st[-1] != dt[c]:
                    return False
                
                st.pop()
            else:
                st.append(c)
        return len(st) == 0