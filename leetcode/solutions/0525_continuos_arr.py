class Solution:
    def findMaxLength(self, nums: List[int]) -> int:
        first_seen = {0: -1}

        balance = 0
        max_len = 0

        # 0 → -1
        # 1 → +1

        for i, num in enumerate(nums):

            if num == 0:
                balance -= 1
            else:
                balance += 1

            if balance in first_seen:
                cur_len = i - first_seen[balance]
                max_len = max(max_len, cur_len)
            else:
                first_seen[balance] = i

        return max_len