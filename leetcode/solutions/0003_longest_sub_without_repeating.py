class Solution:
    def lengthOfLongestSubstring(self, s: str) -> int:
        '''
        while window_invalid():
            shrink()
        '''

        res = set()
        l = 0
        best = 0
        for r in range(len(s)):
            while s[r] in res:
                res.remove(s[l])
                l += 1

            res.add(s[r])
            best = max(best, r - l + 1)

        print(res)

        # Time: O(n), Space: O(n)
        '''
        1. Mở rộng cửa sổ (expand)

        2. Nếu cửa sổ vi phạm điều kiện (invalid)

        3. Thu hẹp từ bên trái (shrink)

        4. Khi cửa sổ hợp lệ

        5. Cập nhật đáp án
        '''
        

        return best

