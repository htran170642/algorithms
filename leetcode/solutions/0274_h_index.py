class Solution:
    def hIndex(self, citations: list[int]) -> int:
        
        citations.sort(reverse=True)

        for i, citation in enumerate(citations):
            h = i + 1

            if citation >= h:
                continue
            else:
                return i

        return len(citations)