from typing import List

class Solution:
    def maxProfit(self, prices: List[int]) -> int:
        ans = 0
        min_price = prices[0]

        for i in range(1, len(prices)):
            min_price = min(min_price, prices[i])
            profit = prices[i] - min_price
            ans = max(ans, profit)

        return ans

        # Time: O(1), Space: O(1)

def main():
    solution = Solution()
    prices = [7,1,5,3,6,4]

    print(solution.maxProfit(prices))

if __name__ == "__main__":
    main()