class Solution:
    def numRescueBoats(self, people: List[int], limit: int) -> int:
        people.sort()

        ans = 0
        l, r = 0, len(people) - 1

        # 1,2,2,3
        while l <= r:
            w = people[l] + people[r]

            if w > limit:
                r -= 1
            else:
                l += 1
                r -= 1

            # if w <= limit:
            #     l += 1
            
            # r -= 1
            ans += 1

        return ans
