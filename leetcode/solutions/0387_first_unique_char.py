class Solution:
    def firstUniqChar(self, s: str) -> int:
        # leetcode

        # cnt = [0]*26

        # for v in s:
        #     cnt[ord(v) - ord('a')] += 1

        # print(cnt)

        cnt = Counter(s)

        for i in range(len(s)):
            if cnt[s[i]] == 1:
                return i

        return -1

