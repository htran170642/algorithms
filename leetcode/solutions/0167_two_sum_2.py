from typing import List

class Solution:
    def twoSum(self, numbers: List[int], target: int) -> List[int]:
        l, r = 0, len(numbers) - 1

        while l < r:
            if numbers[l] + numbers[r] == target:
                return [l+1, r+1]

            if numbers[l] + numbers[r] < target:
                l += 1
            else:
                r -= 1

        # time: O(n), space: O(1)

def main():
    solution = Solution()
    numbers = [2, 7, 11, 15]
    target = 9
    result = solution.twoSum(numbers, target)
    print(result)  # Output: [1, 2]

if __name__ == "__main__":
    main()