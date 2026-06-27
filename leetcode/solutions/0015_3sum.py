class Solution:
    def threeSum(self, nums: list[int]) -> list[list[int]]:
        ans = set()
        nums.sort()

        for k in range(len(nums)):
            target = -nums[k]

            i, j = k + 1, len(nums) - 1
            while i < j:
                if nums[i] + nums[j] == target:
                    ans.add((nums[k], nums[i], nums[j]))
                    i += 1
                    j -= 1
                elif nums[i] + nums[j] > target:
                    j -= 1
                else:
                    i += 1

        # print(ans)

        return [list(val) for val in ans]

def main():
    solution = Solution()
    nums = [-1,0,1,2,-1,-4]
    print(solution.threeSum(nums))

if __name__ == "__main__":
    main()