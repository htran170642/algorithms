    ```bash
    1  #q1
    2  kubectl get pods 
    3  #q2
    4  kubectl run nginx --image=nginx
    5  k get pods
    6  #q3
    7  k get pods
   10  #q4
   11  k get pods
   12  k describe pod newpods-cfwcp
   13  k describe pod newpods-cfwcp | grep -i image
   14  #q5
   15  k get pods -owide
   16  #q6
   17  k get pods
   18  #q7
   19  k describe pod webapp 
   20  #q11
   21  k delete pod webapp 
   23  #q12
   24  k run redis --image=redis123
   25  #q13
   26  k get pod redis -o yaml > q13.yaml
   27  vim q13.yaml 
   28  k delete pod redis 
   29  k apply -f q13.yaml 
   30  k get pod -w
```
