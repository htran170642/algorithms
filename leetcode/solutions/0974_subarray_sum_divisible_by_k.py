class Solution:
    def subarraysDivByK(self, nums: List[int], k: int) -> int:
        
        cnt  = 0

        first_seen = defaultdict(list)
        first_seen[0].append(-1)

        print(first_seen)
        prefix = 0

        for i, num in enumerate(nums):
            prefix += num

            key = prefix % k

            if key in first_seen:
                cnt += len(first_seen[key])

            first_seen[key].append(i)

        return cnt