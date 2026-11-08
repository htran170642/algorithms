```bash
    1  #q1
    2  k get pods
    3  #q2
    4  k get rs
    5  #q3
    6  k get deploy
    7  #q4
    8  k get deploy
    9  #q5
   10  k get rs
   11  #q6
   12* 
   13  #q7
   14  k get pod
   15  #q8
   16  k describe deployments.apps frontend-deployment 
   17  k describe deployments.apps frontend-deployment | grep -i image
   18  #q9
   19  #q10
   20  vim /root/deployment-definition-1.yaml 
   21  kubectl explain deployment | head -n3
   22  k apply -f /root/deployment-definition-1.yaml 
   23  #q11
   24  vim q11.yaml
   25  k apply -f q11.yaml 
   26  k get deployments.apps 
   27  history
```