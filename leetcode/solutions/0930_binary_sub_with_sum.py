class Solution:
    def numSubarraysWithSum(self, nums: list[int], goal: int) -> int:
        count = {0: 1} # prefix_sum: số lần xuất hiện

        prefix = 0
        result = 0

        for num in nums:
            prefix += num

            need = prefix - goal

            if need in count:
                result += count[need]

            count[prefix] = count.get(prefix, 0) + 1

        return result

