class Solution:
    def longestSubstring(self, s: str, k: int) -> int:
        
        def solve(s):
            if len(s) < k:
                return 0

            count = Counter(s)

            for i, ch in enumerate(s):
                if count[ch] < k:
                    left = solve(s[:i])
                    right = solve(s[i+1:])

                    return max(left, right)

            return len(s)

        return solve(s)